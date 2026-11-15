// =====================================================================
//  BOITIER PEDALE D'ACCELERATEUR  (ESP32-S3)
//  Materiel (releve sur le boitier d'origine v1.3.0) :
//    I2C : SDA = GPIO8, SCL = GPIO9, 400 kHz
//    ADS1115 @0x48 : A2 = piste 1 pedale, A3 = piste 2 pedale (lecture directe, +-6,144 V)
//    MCP4725 @0x61 -> piste 1 vers ECU     MCP4725 @0x60 -> piste 2 vers ECU
//  Option conseillee : relais DPDT de bypass sur GPIO10 (USE_RELAY 1).
//
//  Modes : 1 RECOPIE (sorties = pedale) | 2 AIDE (ouverture ajoutee) | 3 DEFAUT (pedale relachee)
//
//  AIDE AU RALENTI (pied leve) :
//   - s'enclenche quand le regime est sous "engage" OU quand il y sera dans
//     "anticip_ms" vu sa vitesse de chute (anticipation des deceleration)
//   - reste enclenchee tant que le pied est leve et le regime sous "off"
//     (au-dessus : frein moteur / coupure d'injection, l'aide se retire)
//   - sortie = plancher(min) + ouverture apprise(I) + P*ecart + D*chute, bornee [min, max]
//   - l'ouverture apprise est conservee entre deux enclenchements (pas de redemarrage a 0)
//   - dans la tolerance +-tol autour du regime vise, l'apprentissage est fige (pas de pompage)
//
//  Tous les reglages sont ecrits en flash ~0,8 s apres une modification.
//  Le boitier garde aussi une copie des reglages du boitier E85 (qui, lui,
//  ne peut pas ecrire en flash moteur tournant).
//  Mise a jour sans fil : recue du boitier E85 via ESP-NOW.
//
//  Carte : ESP32S3 Dev Module, core esp32 Arduino >= 3.0, USB CDC On Boot = Disabled
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include "protocol.h"

// ------------------------------ CONFIG -------------------------------
#define PIN_SDA      8
#define PIN_SCL      9
#define ADS_ADDR     0x48
#define ADS_CH_P1    2          // A2 = piste 1
#define ADS_CH_P2    3          // A3 = piste 2
#define DAC1_ADDR    0x61       // sortie piste 1
#define DAC2_ADDR    0x60       // sortie piste 2
#define VREF_MV      5000       // tension d'alimentation des MCP4725
#define USE_RELAY    0          // 1 si un relais de bypass est cable
#define PIN_RELAY    10
#define LINK_TIMEOUT_MS 500

// ----------------------------- REGLAGES ------------------------------
struct Cal { uint16_t rel1, rel2, full1, full2; };
static const PedConfig CFG_DEFAULT = {
  /*assist_en*/ 0, 0, /*target*/ 950, /*tol*/ 50, /*engage*/ 1300, /*off*/ 2500,
  /*min*/ 0, /*max*/ 150, /*kp*/ 20, /*ki*/ 10, /*kd*/ 5, /*anticip*/ 300, /*release*/ 30,
  /*crit_rpm*/ 0, /*crit_pct*/ 0,         // seuil critique desactive par defaut
  /*gmin*/ 0                              // ouverture minimale generale desactivee par defaut
};
static const Cal CAL_DEFAULT = { 396, 186, 3793, 1888 };   // releve sur le firmware d'origine

static PedConfig CFG;
static Cal       CAL;
static bool      calibrated = false;
static E85Settings E85S;                 // copie des reglages du boitier E85
static Preferences prefs;
static uint32_t dirty_cfg = 0, dirty_cal = 0, dirty_e85 = 0;   // instant de la modif (0 = rien)

static bool cal_valid(const Cal &c) {
  return c.rel1 && c.full1 && abs((int)c.full1 - (int)c.rel1) >= 200 && abs((int)c.full2 - (int)c.rel2) >= 100;
}
static void load_all() {
  CFG = CFG_DEFAULT; CAL = CAL_DEFAULT; memset(&E85S, 0, sizeof(E85S));
  prefs.begin("ped3", true);
  const size_t cl = prefs.getBytesLength("cfg");
  if (cl == sizeof(PedConfig)) prefs.getBytes("cfg", &CFG, sizeof(PedConfig));
  else if (cl >= PEDCONFIG_V21_SIZE && cl < sizeof(PedConfig)) { prefs.getBytes("cfg", &CFG, cl); dirty_cfg = 1; }  // reglages d'une version precedente conserves
  bool have_cal = prefs.getBytesLength("cal") == sizeof(Cal);
  if (have_cal) prefs.getBytes("cal", &CAL, sizeof(Cal));
  if (prefs.getBytesLength("e85") == sizeof(E85Settings)) prefs.getBytes("e85", &E85S, sizeof(E85Settings));
  prefs.end();
  if (!have_cal) {                        // reprise de la calibration de la v2.0 si presente
    prefs.begin("ped2", true);
    uint8_t old[25];
    if (prefs.getBytesLength("s") == 25) {
      prefs.getBytes("s", old, 25);
      Cal c; memcpy(&c, old + 16, sizeof(Cal));
      if (cal_valid(c)) { CAL = c; dirty_cal = 1; }
    }
    prefs.end();
  }
  calibrated = cal_valid(CAL);
  if (!calibrated) CFG.assist_en = 0;
}
static void save_dirty(uint32_t ms) {
  const bool c1 = dirty_cfg && ms - dirty_cfg > 800, c2 = dirty_cal && ms - dirty_cal > 800, c3 = dirty_e85 && ms - dirty_e85 > 300;
  if (!c1 && !c2 && !c3) return;
  prefs.begin("ped3", false);
  if (c1) { prefs.putBytes("cfg", &CFG, sizeof(PedConfig)); dirty_cfg = 0; }
  if (c2) { prefs.putBytes("cal", &CAL, sizeof(Cal));       dirty_cal = 0; }
  if (c3) { prefs.putBytes("e85", &E85S, sizeof(E85Settings)); dirty_e85 = 0; }
  prefs.end();
  Serial.printf("[NVS] ecrit%s%s%s\n", c1 ? " cfg" : "", c2 ? " cal" : "", c3 ? " e85" : "");
}

// ------------------------------ LIEN E85 -----------------------------
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint16_t g_rpm = 0;
static volatile int16_t  g_rate = 0;
static volatile uint8_t  g_engine_on = 0;
static volatile uint32_t g_link_ms = 0;
static volatile bool     g_cfg_new = false;
static PedConfig         g_cfg_rx;
static volatile bool     g_e85_new = false;
static E85Settings       g_e85_rx;
static volatile uint8_t  g_cmd = 0;
static uint16_t          g_last_cmd_seq = 0xFFFF;
static uint16_t          g_seq = 0;
// mise a jour sans fil : un seul message a la fois (protocole question/reponse)
static uint8_t           g_ota_buf[sizeof(MsgOtaData)];
static volatile int      g_ota_len = 0;
static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
static void on_recv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len < (int)sizeof(LinkHeader)) return;
  const LinkHeader *h = (const LinkHeader *)data;
  if (h->magic != (LINK_MAGIC & 0xFFFF) || h->version != LINK_VERSION) return;
  portENTER_CRITICAL(&mux);
  switch (h->type) {
    case MSG_E85_STATUS:
      if (len == sizeof(MsgE85Status)) {
        const MsgE85Status *m = (const MsgE85Status *)data;
        g_rpm = m->rpm; g_rate = m->rpm_rate; g_engine_on = m->engine_on; g_link_ms = millis();
      } break;
    case MSG_PED_CONFIG:   // accepte aussi un boitier E85 d'une version precedente (config plus courte)
      if (len <= (int)sizeof(MsgPedConfig) && len >= (int)(sizeof(LinkHeader) + PEDCONFIG_V21_SIZE)) {
        PedConfig c = CFG;                                      // champs absents -> valeur actuelle
        memcpy(&c, data + sizeof(LinkHeader), len - sizeof(LinkHeader));
        g_cfg_rx = c; g_cfg_new = true;
      } break;
    case MSG_PED_CMD:
      if (len == sizeof(MsgPedCmd) && h->seq != g_last_cmd_seq) { g_last_cmd_seq = h->seq; g_cmd = ((const MsgPedCmd *)data)->cmd; } break;
    case MSG_E85_STORE:
      if (len == sizeof(MsgE85Store)) { g_e85_rx = ((const MsgE85Store *)data)->s; g_e85_new = true; } break;
    case MSG_OTA_BEGIN: case MSG_OTA_DATA: case MSG_OTA_END:
      if (len <= (int)sizeof(g_ota_buf)) { memcpy(g_ota_buf, data, len); g_ota_len = len; } break;
  }
  portEXIT_CRITICAL(&mux);
}

static void hdr(LinkHeader &h, uint8_t type) { h.magic = LINK_MAGIC & 0xFFFF; h.version = LINK_VERSION; h.type = type; h.seq = ++g_seq; }

// ----------------------- MISE A JOUR SANS FIL ------------------------
static esp_ota_handle_t ota_h = 0;
static const esp_partition_t *ota_part = nullptr;
static uint32_t ota_size = 0, ota_next = 0, ota_last_ms = 0;
static bool ota_active = false;

static void ota_ack(uint8_t stage, uint8_t status) {
  MsgOtaAck a = {}; hdr(a.h, MSG_OTA_ACK);
  a.stage = stage; a.status = status; a.next = ota_next;
  esp_now_send(BCAST, (const uint8_t *)&a, sizeof(a));
}
static void ota_abort() {
  if (ota_active) { esp_ota_abort(ota_h); ota_active = false; Serial.println("[OTA] abandon"); }
}
static void ota_process(uint32_t ms) {
  uint8_t buf[sizeof(MsgOtaData)]; int len;
  portENTER_CRITICAL(&mux); len = g_ota_len; if (len) memcpy(buf, g_ota_buf, len); g_ota_len = 0; portEXIT_CRITICAL(&mux);
  if (ota_active && ms - ota_last_ms > 8000) ota_abort();
  if (!len) return;
  ota_last_ms = ms;
  const LinkHeader *h = (const LinkHeader *)buf;
  if (h->type == MSG_OTA_BEGIN && len == sizeof(MsgOtaBegin)) {
    const MsgOtaBegin *b = (const MsgOtaBegin *)buf;
    if (ota_active && b->size == ota_size) { ota_ack(1, OTA_OK); return; }   // doublon
    ota_abort();
    ota_part = esp_ota_get_next_update_partition(nullptr);
    if (!ota_part || b->size > ota_part->size ||
        esp_ota_begin(ota_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_h) != ESP_OK) { ota_ack(1, OTA_ERR_BEGIN); return; }
    ota_active = true; ota_size = b->size; ota_next = 0;
    Serial.printf("[OTA] debut : %lu octets (build %u)\n", (unsigned long)ota_size, b->build);
    ota_ack(1, OTA_OK);
  } else if (h->type == MSG_OTA_DATA && len == sizeof(MsgOtaData)) {
    const MsgOtaData *d = (const MsgOtaData *)buf;
    if (!ota_active) { ota_ack(2, OTA_ERR_BUSY); return; }
    if (d->offset == ota_next && d->len <= OTA_CHUNK && ota_next + d->len <= ota_size) {
      if (esp_ota_write(ota_h, d->data, d->len) != ESP_OK) { ota_abort(); ota_ack(2, OTA_ERR_WRITE); return; }
      ota_next += d->len;
    }
    ota_ack(2, OTA_OK);                  // "next" indique au boitier E85 ou reprendre
  } else if (h->type == MSG_OTA_END && len == sizeof(MsgOtaEnd)) {
    if (!ota_active || ota_next != ota_size) { ota_ack(3, OTA_ERR_BUSY); return; }
    esp_err_t e = esp_ota_end(ota_h); ota_active = false;
    if (e != ESP_OK || esp_ota_set_boot_partition(ota_part) != ESP_OK) { ota_ack(3, OTA_ERR_VERIFY); return; }
    for (int k = 0; k < 3; k++) { ota_ack(3, OTA_OK); delay(20); }
    Serial.println("[OTA] succes -> redemarrage");
    save_dirty(ms + 100000);
    delay(200);
    esp_restart();
  }
}

// ----------------------------- ADS1115 -------------------------------
static bool ads_read_mv(uint8_t ch, uint16_t &mv) {
  const uint16_t cfg = 0x8000 | ((0x4 + ch) << 12) | 0x0100 | (0x7 << 5) | 0x0003;  // single, +-6,144 V, 860 SPS
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  const uint32_t t0 = micros();
  for (;;) {
    delayMicroseconds(150);
    Wire.beginTransmission(ADS_ADDR); Wire.write(0x01);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADS_ADDR, 2) != 2) return false;
    const uint16_t c = (Wire.read() << 8) | Wire.read();
    if (c & 0x8000) break;
    if (micros() - t0 > 4000) return false;
  }
  Wire.beginTransmission(ADS_ADDR); Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADS_ADDR, 2) != 2) return false;
  int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
  if (raw < 0) raw = 0;
  mv = (uint16_t)((raw * 3UL) / 16UL);          // x0,1875 mV
  return true;
}

// ----------------------------- MCP4725 -------------------------------
static bool dac_write(uint8_t addr, uint16_t mv) {
  uint32_t code = (uint32_t)mv * 4095UL / VREF_MV;
  if (code > 4095) code = 4095;
  Wire.beginTransmission(addr);
  Wire.write((code >> 8) & 0x0F); Wire.write(code & 0xFF);
  return Wire.endTransmission() == 0;
}

// ------------------------------- ETAT --------------------------------
static float f_in1 = 0, f_in2 = 0;
static float assist = 0, learned = 0;
static bool  active = false;
static bool  critical = false;   // regime sous le seuil critique
static uint8_t mode = 1;
static uint16_t out1 = 0, out2 = 0;
static float pedal_pct = 0, out_pct = 0;
static bool  g_ads_ok = false, g_dac_ok = false;
static uint8_t ads_err = 0;
static uint32_t relay_on_ms = 0;

static void send_telem() {
  MsgPedTelem t = {}; hdr(t.h, MSG_PED_TELEM);
  t.in1_mv = (uint16_t)f_in1; t.in2_mv = (uint16_t)f_in2;
  t.out1_mv = out1; t.out2_mv = out2; t.vref_mv = VREF_MV;
  t.pedal_pct10   = (uint16_t)constrain(pedal_pct * 10, 0, 1000);
  t.out_pct10     = (uint16_t)constrain(out_pct * 10, 0, 1000);
  t.assist_pct10  = (uint16_t)constrain(assist * 10, 0, 1000);
  t.learned_pct10 = (uint16_t)constrain(learned * 10, 0, 1000);
  t.mode = mode;
  const bool link = g_link_ms && (millis() - g_link_ms) < LINK_TIMEOUT_MS;
  t.flags = (calibrated ? 1 : 0) | (link ? 2 : 0) | ((dirty_cfg || dirty_cal || dirty_e85) ? 4 : 0)
          | ((g_dac_ok && g_ads_ok) ? 8 : 0) | (active ? 16 : 0) | (critical ? 32 : 0);
  t.cal_rel1 = CAL.rel1; t.cal_rel2 = CAL.rel2; t.cal_full1 = CAL.full1; t.cal_full2 = CAL.full2;
  t.e85_rev = E85S.rev; t.fw_build = PROJECT_BUILD;
  t.cfg = CFG;
  esp_now_send(BCAST, (const uint8_t *)&t, sizeof(t));
}
static void send_e85_copy() {
  if (!E85S.rev) return;
  MsgE85Store m = {}; hdr(m.h, MSG_E85_RESTORE); m.s = E85S;
  esp_now_send(BCAST, (const uint8_t *)&m, sizeof(m));
}

static void handle_cmd(uint8_t c, uint32_t ms) {
  switch (c) {
    case CMD_CAL_RELEASED: CAL.rel1 = f_in1; CAL.rel2 = f_in2; dirty_cal = ms; break;
    case CMD_CAL_FLOORED:  CAL.full1 = f_in1; CAL.full2 = f_in2; dirty_cal = ms; break;
    case CMD_SAVE:         break;                                   // tout est automatique
    case CMD_DEFAULTS:     CFG = CFG_DEFAULT; CAL = CAL_DEFAULT; dirty_cfg = dirty_cal = ms; learned = 0; break;
    case CMD_SEND_E85:     send_e85_copy(); break;
  }
  calibrated = cal_valid(CAL);
  if (!calibrated && CFG.assist_en) { CFG.assist_en = 0; dirty_cfg = ms; }
  Serial.printf("[CMD] %u -> cal=%d rel=%u/%u full=%u/%u\n", c, calibrated, CAL.rel1, CAL.rel2, CAL.full1, CAL.full2);
}

void setup() {
#if USE_RELAY
  pinMode(PIN_RELAY, OUTPUT); digitalWrite(PIN_RELAY, LOW);
#endif
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n[BOOT] Boitier pedale v%s (build %d) reset=%d\n", PROJECT_VERSION, PROJECT_BUILD, (int)esp_reset_reason());
  load_all();
  Serial.printf("[NVS] cal=%d rel=%u/%u full=%u/%u  copieE85 rev=%lu\n", calibrated, CAL.rel1, CAL.rel2, CAL.full1, CAL.full2, (unsigned long)E85S.rev);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setTimeOut(5);
  g_dac_ok = dac_write(DAC1_ADDR, CAL.rel1) & dac_write(DAC2_ADDR, CAL.rel2);   // pedale relachee d'abord
  uint16_t v;
  g_ads_ok = ads_read_mv(ADS_CH_P1, v);
  Serial.printf("[I2C] MCP4725 0x61/0x60 %s  ADS1115 0x48 %s\n", g_dac_ok ? "OK" : "ERREUR", g_ads_ok ? "OK" : "ERREUR");
  if (g_ads_ok) { f_in1 = v; if (ads_read_mv(ADS_CH_P2, v)) f_in2 = v; }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(on_recv);
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, BCAST, 6); p.channel = 0; p.ifidx = WIFI_IF_STA; p.encrypt = false;
    esp_now_add_peer(&p);
    Serial.println("[ESPNOW] OK (canal 1)");
  }

  // chien de garde 1,5 s (une ecriture flash peut durer quelques centaines de ms)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wc = { .timeout_ms = 1500, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wc);
#else
  esp_task_wdt_init(2, true);
#endif
  esp_task_wdt_add(NULL);
  esp_ota_mark_app_valid_cancel_rollback();
}

void loop() {
  static uint32_t t_telem = 0, t_log = 0, last_us = micros();
  esp_task_wdt_reset();
  const uint32_t ms = millis();

  // --- messages recus ---
  uint8_t cmd; bool cfgn, e85n; PedConfig cfg; E85Settings e85; uint16_t rpm; int16_t rate; uint8_t eng; uint32_t lms;
  portENTER_CRITICAL(&mux);
  cmd = g_cmd; g_cmd = 0; cfgn = g_cfg_new; g_cfg_new = false; cfg = g_cfg_rx;
  e85n = g_e85_new; g_e85_new = false; e85 = g_e85_rx;
  rpm = g_rpm; rate = g_rate; eng = g_engine_on; lms = g_link_ms;
  portEXIT_CRITICAL(&mux);
  if (cmd) handle_cmd(cmd, ms);
  if (cfgn && memcmp(&cfg, &CFG, sizeof(PedConfig)) != 0) {
    CFG = cfg; if (!calibrated) CFG.assist_en = 0;
    dirty_cfg = ms;
  }
  if (e85n && e85.rev > E85S.rev) { E85S = e85; dirty_e85 = ms; }
  const bool link = lms && (ms - lms) < LINK_TIMEOUT_MS;
  ota_process(ms);

  // --- mesures (2 conversions ADS, ~2,6 ms) ---
  uint16_t m1, m2;
  if (ads_read_mv(ADS_CH_P1, m1) && ads_read_mv(ADS_CH_P2, m2)) {
    f_in1 += 0.6f * (m1 - f_in1);
    f_in2 += 0.6f * (m2 - f_in2);
    ads_err = 0; g_ads_ok = true;
  } else if (++ads_err >= 3) {
    g_ads_ok = false;
  }
  const uint32_t now_us = micros();
  float dt = (now_us - last_us) * 1e-6f; last_us = now_us;
  if (dt > 0.1f) dt = 0.1f;

  // --- position pedale conducteur ---
  pedal_pct = 0;
  if (calibrated) {
    pedal_pct = 100.0f * (f_in1 - CAL.rel1) / (float)((int)CAL.full1 - (int)CAL.rel1);
    pedal_pct = constrain(pedal_pct, 0.0f, 100.0f);
  }

  // ===================== AIDE AU RALENTI =====================
  const PedConfig &c = CFG;
  const bool released = pedal_pct < c.release_pct10 / 10.0f;
  const bool usable = link && c.assist_en && calibrated && eng && rpm > 0 && !ota_active;
  const float predicted = (float)rpm + (float)rate * c.anticip_ms / 1000.0f;   // rate < 0 en deceleration
  const float minp = c.min_pct10 / 10.0f, maxp = max(c.max_pct10 / 10.0f, minp);

  if (!usable || !released) {
    active = false;
  } else if (!active) {
    if (rpm < c.off_rpm && (rpm < c.engage_rpm || predicted < c.engage_rpm)) {
      active = true;
      assist = max(assist, minp + learned);                // enclenchement direct, sans rampe
    }
  } else if (rpm > c.off_rpm) {
    active = false;                                       // frein moteur : on rend la main
  }

  float target = 0;
  if (active) {
    const float err  = ((float)c.target_rpm - rpm) / 100.0f;       // en centaines de tr/min
    const float P    = (c.kp10 / 10.0f) * err;
    const float D    = -(c.kd10 / 10.0f) * (rate / 100.0f);          // chute rapide -> ouvre plus tot
    float u = minp + learned + P + D;
    // apprentissage (integrale) hors tolerance, avec anti-emballement
    if (fabsf((float)c.target_rpm - rpm) > c.tol_rpm) {
      const bool sat_hi = u >= maxp && err > 0, sat_lo = u <= minp && err < 0;
      if (!sat_hi && !sat_lo) learned += (c.ki10 / 10.0f) * err * dt;
      learned = constrain(learned, 0.0f, maxp - minp);
    }
    target = constrain(minp + learned + P + D, minp, maxp);
  }
  // rampes : montee rapide (300 %/s), descente douce (40 %/s) ; plancher immediat
  if (target > assist) assist = min(target, assist + 300.0f * dt);
  else                 assist = max(target, assist - 40.0f * dt);
  if (active && assist < minp) assist = minp;

  // --- SEUIL CRITIQUE : sous ce regime, ouverture forcee IMMEDIATE (sans rampe) ---
  // S'applique pied leve comme pied pose : le calculateur recoit le plus grand des deux.
  critical = false;
  if (link && c.assist_en && calibrated && eng && rpm > 0 && !ota_active && c.crit_rpm && rpm < c.crit_rpm) {
    critical = true;
    const float cp = c.crit_pct10 / 10.0f;
    if (assist < cp) assist = cp;
  }

  // --- OUVERTURE MINIMALE GENERALE : la pedale ne descend jamais sous gmin, avec ou sans aide ---
  // (garde l'injection active en deceleration -> le regime reste mesurable)
  const float gmin = calibrated ? c.gmin_pct10 / 10.0f : 0.0f;
  const float synth = max(assist, gmin);             // ouverture synthetisee demandee

  // --- sorties ---
  const bool in_ok = f_in1 > 100 && f_in1 < 4950 && f_in2 > 50 && f_in2 < 4950;
  if (!g_ads_ok || !in_ok)                          mode = 3;
  else if (synth > 0.05f && synth > pedal_pct)      mode = 2;
  else                                              mode = 1;

  if (mode == 3) {
    out_pct = 0; out1 = CAL.rel1; out2 = CAL.rel2;
  } else if (mode == 2) {
    out_pct = synth;
    out1 = CAL.rel1 + (int)(((int)CAL.full1 - (int)CAL.rel1) * synth / 100.0f);
    out2 = CAL.rel2 + (int)(((int)CAL.full2 - (int)CAL.rel2) * synth / 100.0f);
  } else {
    out_pct = pedal_pct;
    out1 = (uint16_t)f_in1; out2 = (uint16_t)f_in2;
  }
  g_dac_ok = dac_write(DAC1_ADDR, out1) & dac_write(DAC2_ADDR, out2);

#if USE_RELAY
  if (mode != 3 && g_dac_ok) {
    if (!relay_on_ms) relay_on_ms = ms;
    if (ms - relay_on_ms > 200) digitalWrite(PIN_RELAY, HIGH);
  } else { relay_on_ms = 0; digitalWrite(PIN_RELAY, LOW); }
#endif

  if (!ota_active) save_dirty(ms);

  if (ms - t_telem >= 50) { t_telem = ms; send_telem(); }
  if (ms - t_log >= 2000) {
    t_log = ms;
    Serial.printf("[PED] in=%u/%u out=%u/%u ped=%.1f%% aide=%.1f%% appris=%.1f%% act=%d mode=%u rpm=%u (%d/s) lien=%d ads=%d dac=%d\n",
      (unsigned)f_in1, (unsigned)f_in2, out1, out2, pedal_pct, assist, learned, active, mode, rpm, rate, link, g_ads_ok, g_dac_ok);
  }
}
