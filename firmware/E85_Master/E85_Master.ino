// =====================================================================
//  BOITIER E85 - MASTER  (ESP32-S3)            v2.0 - reconstruction 2026
// ---------------------------------------------------------------------
//  Entrees  : PC817 -> GPIO4 (cyl 1), GPIO5 (cyl 2), GPIO6 (cyl 3)
//             niveau HAUT (3.3 V) = l'ECU commande l'injection
//  Sorties  : GPIO7 (cyl 1), GPIO15 (cyl 2), GPIO16 (cyl 3) -> gate IRLB8721
//             niveau HAUT = injecteur ouvert
//
//  PRINCIPE TEMPS REEL
//  Le coeur 1 du S3 est entierement dedie a une boucle en IRAM qui lit
//  directement le registre GPIO et le compteur de cycles CPU (240 MHz =
//  resolution 4,2 ns). Une iteration complete (3 cylindres) prend ~0,1 us.
//   - Front montant ECU  -> sortie ON immediatement (latence < 0,5 us + filtre)
//   - Front descendant   -> on mesure la duree ECU, on calcule le
//                           prolongement et on coupe la sortie a l'instant
//                           exact (fin ECU + prolongement).
//  Pas d'interruption, pas de timer logiciel, pas d'appel systeme :
//  la gigue est de l'ordre de la microseconde, quel que soit le Wi-Fi.
//  Tout le reste (Wi-Fi, page web, ESP-NOW) tourne sur le coeur 0.
//
//  REGLAGES CARTE (Arduino IDE / arduino-cli), OBLIGATOIRES :
//    Carte            : ESP32S3 Dev Module   (core esp32 Arduino >= 3.0)
//    Arduino Runs On  : Core 1
//    Events Run On    : Core 0      <-- important
//    CPU Frequency    : 240 MHz
//    Flash Size       : 16MB  | Partition : 16M Flash (3MB APP/9.9MB FATFS) ou defaut
//    PSRAM            : peu importe
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <esp_wifi.h>
#include <esp_cpu.h>
#include <esp_system.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <driver/gpio.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>

#include "protocol.h"
#include "web_ui.h"
#include "pedal_fw.h"        // firmware du boitier pedale (genere par tools/build_all.ps1)
// Retour arriere automatique : un nouveau programme demarre "a l'essai" et ne se valide
// qu'apres 20 s de bon fonctionnement (voir comm_task). Plantage avant -> ancienne version.

// ------------------------------ CONFIG -------------------------------
static const char *AP_SSID = "BoitierE85";
static String AP_PASS = "ethanol123";               // par defaut ; modifiable depuis l'appli (memorise)
static const uint8_t PIN_IN [3] = { 4, 5, 6 };
static const uint8_t PIN_OUT[3] = { 7, 15, 16 };
#define FW_VERSION PROJECT_VERSION

// ----------------------------- REGLAGES ------------------------------
// Reglages : struct E85Settings (protocol.h), aussi memorisee dans le boitier pedale.
typedef E85Settings Settings;
static const Settings DEFAULTS = { 0, 1, 0, 1, 0, 250, {0,0,0}, 0, 15000, 90, 30, 900, 3000 };
static Settings S;
static Preferences prefs;
static uint32_t g_saved_rev = 0;          // revision ecrite dans la flash du boitier E85

// ------------------ parametres temps reel (derives) ------------------
// Ecrits par le coeur 0, lus par la boucle temps reel (coeur 1).
static volatile uint32_t rt_enabled;
static volatile uint32_t rt_inv;
static volatile uint32_t rt_fq[3];        // facteur prolongement Q16 (65536 = +100 %)
static volatile uint32_t rt_dead_t;       // en cycles CPU
static volatile uint32_t rt_maxext_t;
static volatile uint32_t rt_dq;           // rapport cyclique maxi Q16
static volatile uint32_t rt_filt_t;
static volatile uint32_t rt_sim;
static volatile uint32_t rt_sim_period_t;
static volatile uint32_t rt_sim_pw_t;
static volatile uint32_t rt_sim_off_t[3];
static uint32_t TPU = 240;                // cycles CPU par microseconde
static uint32_t PERIOD_MIN_T, PERIOD_MAX_T;

struct Cyl {
  uint32_t in_shift, out_mask;
  uint32_t stable, pend, pend_t, raw_last;
  uint32_t t_start, last_start, has_start, period;
  uint32_t ext_active, off_at;
  // statistiques (lues par le coeur 0)
  volatile uint32_t pulses, width_t, ext_t, period_t, raw_edges;
};
static Cyl cyl[3];

// ============================ TEMPS REEL ==============================
static inline void IRAM_ATTR out_on (Cyl &c) { REG_WRITE(GPIO_OUT_W1TS_REG, c.out_mask); }
static inline void IRAM_ATTR out_off(Cyl &c) { REG_WRITE(GPIO_OUT_W1TC_REG, c.out_mask); }

// noinline : sinon le compilateur l'integre dans setup() (en flash) et perd l'IRAM
static void __attribute__((noinline)) IRAM_ATTR rt_loop() {
  uint32_t last = esp_cpu_get_cycle_count();
  uint32_t sim_phase = 0;
  for (;;) {
    const uint32_t now = esp_cpu_get_cycle_count();
    const uint32_t gin = REG_READ(GPIO_IN_REG);
    const uint32_t sim = rt_sim;
    const uint32_t dt  = now - last; last = now;
    uint32_t sp = 0, spw = 0;
    if (sim) {
      sp = rt_sim_period_t; spw = rt_sim_pw_t;
      sim_phase += dt;
      if (sp) while (sim_phase >= sp) sim_phase -= sp;
    }
    const uint32_t filt = rt_filt_t;

    for (int i = 0; i < 3; i++) {
      Cyl &c = cyl[i];
      const uint32_t raw = (gin >> c.in_shift) & 1u;
      if (raw != c.raw_last) { c.raw_last = raw; c.raw_edges++; }
      uint32_t lvl;
      if (sim) {
        uint32_t ph = sim_phase + rt_sim_off_t[i];
        if (ph >= sp) ph -= sp;
        lvl = ph < spw;
      } else {
        lvl = raw ^ rt_inv;
      }

      // --- filtre : un changement doit durer 'filt' pour etre accepte ---
      if (lvl != c.stable) {
        if (!c.pend) { c.pend = 1; c.pend_t = now; }
        else if (now - c.pend_t >= filt) {
          const uint32_t t = c.pend_t;          // instant reel du front
          c.stable = lvl; c.pend = 0;
          if (lvl) {
            // ===== DEBUT INJECTION ECU =====
            out_on(c);
            c.ext_active = 0;
            if (c.has_start) {
              const uint32_t p = t - c.last_start;
              c.period = (p >= PERIOD_MIN_T && p <= PERIOD_MAX_T) ? p : 0;
              c.period_t = c.period;
            }
            c.last_start = t; c.has_start = 1; c.t_start = t;
            c.pulses++;
          } else {
            // ===== FIN INJECTION ECU -> PROLONGEMENT =====
            const uint32_t w = t - c.t_start;
            uint32_t ext = 0;
            const uint32_t dead = rt_dead_t;
            if (rt_enabled && w > dead) {
              ext = (uint32_t)(((uint64_t)(w - dead) * rt_fq[i]) >> 16);
              const uint32_t mx = rt_maxext_t;
              if (ext > mx) ext = mx;
              if (c.period) {
                const uint32_t maxon = (uint32_t)(((uint64_t)c.period * rt_dq) >> 16);
                ext = (w + ext > maxon) ? (maxon > w ? maxon - w : 0) : ext;
              }
            }
            c.width_t = w; c.ext_t = ext;
            if (ext) { c.off_at = t + ext; c.ext_active = 1; }
            else     { out_off(c); }
          }
        }
      } else {
        c.pend = 0;
      }

      // --- fin du prolongement ---
      if (c.ext_active && (int32_t)(now - c.off_at) >= 0) {
        out_off(c);
        c.ext_active = 0;
      }
    }
  }
}

// ========================== REGLAGES -> RT ============================
static void apply_settings() {
  S.pct10   = constrain(S.pct10, 0, 1500);          // 0..+150 %
  for (int i = 0; i < 3; i++) S.trim10[i] = constrain(S.trim10[i], -300, 300);
  S.dead_us   = constrain(S.dead_us, 0, 3000);
  S.maxext_us = constrain(S.maxext_us, 500, 30000);
  S.maxduty   = constrain(S.maxduty, 30, 98);
  S.filt_us10 = constrain(S.filt_us10, 0, 200);
  S.ipc       = (S.ipc == 2) ? 2 : 1;
  S.sim_rpm   = constrain(S.sim_rpm, 300, 8000);
  S.sim_pw_us = constrain(S.sim_pw_us, 300, 30000);

  for (int i = 0; i < 3; i++) {
    int32_t p = S.pct10 + S.trim10[i]; if (p < 0) p = 0;
    rt_fq[i] = (uint32_t)((p * 65536UL) / 1000UL);
  }
  rt_dead_t   = S.dead_us * TPU;
  rt_maxext_t = S.maxext_us * TPU;
  rt_dq       = (uint32_t)S.maxduty * 65536UL / 100UL;
  rt_filt_t   = S.filt_us10 * TPU / 10;
  rt_inv      = S.inv ? 1 : 0;
  // simulateur : periode par cylindre = 120e6/(rpm*ipc) us
  uint32_t per_us = 120000000UL / ((uint32_t)S.sim_rpm * S.ipc);
  uint32_t pw_us  = min<uint32_t>(S.sim_pw_us, per_us * 8 / 10);
  rt_sim_pw_t = pw_us * TPU;
  for (int i = 0; i < 3; i++) rt_sim_off_t[i] = (per_us * TPU) / 3 * i;
  rt_sim_period_t = per_us * TPU;
  rt_enabled = S.enabled ? 1 : 0;
}

static void load_settings() {
  S = DEFAULTS;
  prefs.begin("e85", true);
  if (prefs.getBytesLength("s2") == sizeof(Settings)) prefs.getBytes("s2", &S, sizeof(Settings));
  String p = prefs.getString("pass", "");
  if (p.length() >= 8) AP_PASS = p;
  prefs.end();
  g_saved_rev = S.rev;
}
static void write_settings() {
  prefs.begin("e85", false);
  prefs.putBytes("s2", &S, sizeof(Settings));
  prefs.end();
  g_saved_rev = S.rev;
}

// ============================= ETAT MOTEUR =============================
static volatile uint16_t g_rpm = 0;
static volatile bool     g_engine_on = false;
static uint32_t g_last_pulse_ms[3] = {0, 0, 0};
static uint32_t g_prev_pulses[3]   = {0, 0, 0};
static float    g_rpm_f = 0;
static float    g_rate_f = 0;             // derivee du regime (tr/min/s)
static float    g_rpm_prev = 0;
static uint32_t g_rate_ms = 0;
static uint32_t g_engine_off_since = 0;
static uint32_t g_sim_raw_base = 0;

static void update_engine() {
  const uint32_t ms = millis();
  // derivee filtree (~100 ms) - sert a l'anticipation de l'aide au ralenti
  if (g_rate_ms) {
    const float dt = (ms - g_rate_ms) / 1000.0f;
    if (dt > 0.005f) {
      const float r = (g_rpm_f - g_rpm_prev) / dt;
      g_rate_f += 0.25f * (r - g_rate_f);
    }
  }
  g_rpm_prev = g_rpm_f; g_rate_ms = ms;
  uint32_t sum_us = 0, n = 0; bool any = false;
  for (int i = 0; i < 3; i++) {
    uint32_t p = cyl[i].pulses;
    if (p != g_prev_pulses[i]) { g_prev_pulses[i] = p; g_last_pulse_ms[i] = ms; }
    if (ms - g_last_pulse_ms[i] < 700 && g_last_pulse_ms[i]) {
      any = true;
      uint32_t pt = cyl[i].period_t;
      if (pt) { sum_us += pt / TPU; n++; }
    }
  }
  if (any && n) {
    float rpm = 120000000.0f / ((float)(sum_us / n) * S.ipc);
    g_rpm_f = (g_rpm_f < 1) ? rpm : g_rpm_f + 0.4f * (rpm - g_rpm_f);
  } else if (!any) { g_rpm_f = 0; g_rate_f = 0; }
  g_rpm = (uint16_t)constrain((int)(g_rpm_f + 0.5f), 0, 12000);
  if (any != g_engine_on) { g_engine_on = any; if (!any) g_engine_off_since = ms; }
}

// ============================== ESP-NOW ===============================
static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint16_t g_seq = 0;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static MsgPedTelem g_telem;
static volatile uint32_t g_telem_ms = 0;
static volatile bool g_telem_valid = false;
static volatile uint8_t  g_ack_stage = 0, g_ack_status = 0;
static volatile uint32_t g_ack_next = 0;
static volatile bool     g_ack_new = false;
static E85Settings g_restore;
static volatile bool g_restore_new = false;
static PedConfig g_ped_des;
enum { POTA_IDLE = 0, POTA_RUN = 1, POTA_OK = 2, POTA_FAIL = 3, POTA_SENT = 4 };   // SENT = envoye a l'aveugle, sans confirmation
static volatile bool     g_pota_blind = false;
static volatile uint32_t g_pota_end_ms = 0;
static volatile int      g_pota_state = POTA_IDLE;
static volatile uint32_t g_pota_done = 0;
static volatile uint8_t  g_pota_err = 0;
static uint32_t g_pota_last_try = 0;
static uint8_t  g_pota_tries = 0;
static volatile bool g_e85_ota = false;             // mise a jour du boitier E85 en cours
static uint32_t  g_store_ms = 0;         // dernier envoi de la copie des reglages a la pedale
static uint32_t  g_sync_ms = 0;
static bool      g_ped_pending = false;
static uint32_t  g_ped_pending_ms = 0;

static volatile uint32_t g_rx_n = 0, g_rx_ms = 0;
static volatile int      g_rx_len = 0;
static volatile uint8_t  g_rx_type = 0, g_rx_ver = 0;
static volatile uint16_t g_rx_magic = 0;

static void fill_hdr(LinkHeader &h, uint8_t type) {
  h.magic = LINK_MAGIC & 0xFFFF; h.version = LINK_VERSION; h.type = type; h.seq = ++g_seq;
}

// ---- envoi DIRECT a la pedale des que son adresse est connue (accuse radio + repetitions) ----
// Le regime (50 Hz) reste en diffusion : c'est grace a lui qu'une nouvelle pedale trouve le boitier.
static uint8_t  g_ped_mac[6];
static volatile bool g_ped_mac_new = false;
static bool     g_ped_peer = false;
static volatile uint32_t g_tx_ok = 0, g_tx_fail = 0;

static void tx_ped(const void *msg, size_t len) {
  esp_now_send(g_ped_peer ? g_ped_mac : BCAST, (const uint8_t *)msg, len);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void on_sent(const wifi_tx_info_t *info, esp_now_send_status_t st) {
  const uint8_t *dst = info ? info->des_addr : nullptr;
#else
static void on_sent(const uint8_t *dst, esp_now_send_status_t st) {
#endif
  if (!dst || dst[0] == 0xFF) return;            // diffusion : pas d'accuse
  if (st == ESP_NOW_SEND_SUCCESS) g_tx_ok++; else g_tx_fail++;
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *src = info->src_addr;
#else
static void on_recv(const uint8_t *src, const uint8_t *data, int len) {
#endif
  // diagnostic : on compte TOUT ce qui arrive, meme rejete (pedale muette ou incomprise ?)
  g_rx_n++; g_rx_ms = millis(); g_rx_len = len;
  if (len >= (int)sizeof(LinkHeader)) {
    const LinkHeader *hh = (const LinkHeader *)data;
    g_rx_type = hh->type; g_rx_ver = hh->version; g_rx_magic = hh->magic;
  }
  if (len < (int)sizeof(LinkHeader)) return;
  const LinkHeader *h = (const LinkHeader *)data;
  if (h->magic != (LINK_MAGIC & 0xFFFF) || h->version != LINK_VERSION) return;
  // telemetrie : accepte aussi les pedales d'une version precedente (PedConfig plus courte)
  // -> indispensable pour pouvoir leur envoyer la mise a jour
  if (h->type == MSG_PED_TELEM && len <= (int)sizeof(MsgPedTelem) && len >= (int)PED_TELEM_MIN_SIZE) {
    if (!g_ped_peer || memcmp(src, g_ped_mac, 6) != 0) { memcpy(g_ped_mac, src, 6); g_ped_mac_new = true; }
    portENTER_CRITICAL(&g_mux);
    memset(&g_telem, 0, sizeof(MsgPedTelem));
    memcpy(&g_telem, data, len);
    g_telem_ms = millis(); g_telem_valid = true;
    portEXIT_CRITICAL(&g_mux);
  } else if (h->type == MSG_OTA_ACK && len == sizeof(MsgOtaAck)) {
    const MsgOtaAck *a = (const MsgOtaAck *)data;
    g_ack_stage = a->stage; g_ack_status = a->status; g_ack_next = a->next; g_ack_new = true;
  } else if (h->type == MSG_E85_RESTORE && len == sizeof(MsgE85Store)) {
    portENTER_CRITICAL(&g_mux);
    g_restore = ((const MsgE85Store *)data)->s; g_restore_new = true;
    portEXIT_CRITICAL(&g_mux);
  }
}

static void send_status() {
  MsgE85Status m = {};
  fill_hdr(m.h, MSG_E85_STATUS);
  m.rpm = g_rpm; m.engine_on = g_engine_on ? 1 : 0;
  m.rpm_rate = (int16_t)constrain((int)g_rate_f, -30000, 30000);
  esp_now_send(BCAST, (const uint8_t *)&m, sizeof(m));
}
static void send_store() {
  MsgE85Store m = {};
  fill_hdr(m.h, MSG_E85_STORE); m.s = S;
  tx_ped(&m, sizeof(m));
}
static void send_ped_cfg(const PedConfig &c) {
  MsgPedConfig m = {};
  fill_hdr(m.h, MSG_PED_CONFIG); m.cfg = c;
  tx_ped(&m, sizeof(m));
}
static void send_ped_cmd(uint8_t cmd) {
  MsgPedCmd m = {};
  fill_hdr(m.h, MSG_PED_CMD); m.cmd = cmd;
  for (int k = 0; k < 3; k++) { tx_ped(&m, sizeof(m)); delay(5); }
}
static bool ped_link() { return g_telem_valid && (millis() - g_telem_ms) < 600; }

// ============================== WEB / API ==============================
static WebServer server(80);
static char jbuf[4096];

static void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
}

static int json_extra(char *b, size_t sz);   // defini plus bas (mise a jour E85 + incidents)

static void send_json() {
  MsgPedTelem t; uint32_t tms; bool tv;
  portENTER_CRITICAL(&g_mux); t = g_telem; tms = g_telem_ms; tv = g_telem_valid; portEXIT_CRITICAL(&g_mux);
  const bool link = tv && (millis() - tms) < 600;
  // etat de sauvegarde : 0 = en flash E85, 1 = memorise dans la pedale (flash E85 a l'arret), 2 = en attente
  const int sv = (g_saved_rev == S.rev) ? 0 : (link && t.e85_rev == S.rev) ? 1 : 2;
  int n = 0;
  n += snprintf(jbuf + n, sizeof(jbuf) - n,
    "{\"fw\":\"" FW_VERSION "\",\"build\":%d,\"rpm\":%u,\"rate\":%d,\"on\":%d,\"en\":%d,\"pct\":%.1f,\"trim\":[%.1f,%.1f,%.1f],"
    "\"dead\":%u,\"maxext\":%u,\"maxduty\":%u,\"filt\":%.1f,\"inv\":%d,\"ipc\":%d,"
    "\"sim\":%d,\"simrpm\":%u,\"simpw\":%u,\"sv\":%d,\"rev\":%lu,\"up\":%lu,\"rst\":%d,\"heap\":%lu,\"cyl\":[",
    PROJECT_BUILD, g_rpm, (int)g_rate_f, g_engine_on, S.enabled, S.pct10 / 10.0f, S.trim10[0] / 10.0f, S.trim10[1] / 10.0f, S.trim10[2] / 10.0f,
    S.dead_us, S.maxext_us, S.maxduty, S.filt_us10 / 10.0f, S.inv, S.ipc,
    (int)rt_sim, S.sim_rpm, S.sim_pw_us, sv, (unsigned long)S.rev, millis() / 1000, (int)esp_reset_reason(),
    (unsigned long)ESP.getFreeHeap());
  for (int i = 0; i < 3; i++) {
    const bool act = g_last_pulse_ms[i] && (millis() - g_last_pulse_ms[i] < 700);
    uint32_t pw = act ? cyl[i].width_t / TPU : 0, ex = act ? cyl[i].ext_t / TPU : 0, pe = act ? cyl[i].period_t / TPU : 0;
    float duty = pe ? 100.0f * (pw + ex) / pe : 0;
    n += snprintf(jbuf + n, sizeof(jbuf) - n, "%s{\"pw\":%lu,\"ext\":%lu,\"per\":%lu,\"duty\":%.1f,\"n\":%lu}",
                  i ? "," : "", (unsigned long)pw, (unsigned long)ex, (unsigned long)pe, duty, (unsigned long)cyl[i].pulses);
  }
  n += snprintf(jbuf + n, sizeof(jbuf) - n, "],\"ped\":{\"link\":%d", link);
  if (tv) {
    const PedConfig &c = t.cfg;
    n += snprintf(jbuf + n, sizeof(jbuf) - n,
      ",\"build\":%u,\"age\":%lu,\"in1\":%u,\"in2\":%u,\"out1\":%u,\"out2\":%u,\"vref\":%u,\"pp\":%.1f,\"op\":%.1f,\"ap\":%.1f,\"lrn\":%.1f,"
      "\"mode\":%u,\"flags\":%u,\"cal\":[%u,%u,%u,%u],"
      "\"cfg\":{\"aen\":%u,\"tgt\":%u,\"tol\":%u,\"eng\":%u,\"off\":%u,\"min\":%.1f,\"max\":%.1f,"
      "\"kp\":%.1f,\"ki\":%.1f,\"kd\":%.1f,\"ant\":%u,\"rel\":%.1f,\"crit\":%u,\"critp\":%.1f,\"gmin\":%.1f}",
      t.fw_build, (unsigned long)(millis() - tms), t.in1_mv, t.in2_mv, t.out1_mv, t.out2_mv, t.vref_mv,
      t.pedal_pct10 / 10.0f, t.out_pct10 / 10.0f, t.assist_pct10 / 10.0f, t.learned_pct10 / 10.0f, t.mode, t.flags,
      t.cal_rel1, t.cal_rel2, t.cal_full1, t.cal_full2,
      c.assist_en, c.target_rpm, c.tol_rpm, c.engage_rpm, c.off_rpm, c.min_pct10 / 10.0f, c.max_pct10 / 10.0f,
      c.kp10 / 10.0f, c.ki10 / 10.0f, c.kd10 / 10.0f, c.anticip_ms, c.release_pct10 / 10.0f,
      c.crit_rpm, c.crit_pct10 / 10.0f, c.gmin_pct10 / 10.0f);
  }
  if (tv) n += snprintf(jbuf + n, sizeof(jbuf) - n, ",\"txok\":%u,\"txfail\":%u,\"prst\":%u,\"direct\":%u,\"pup\":%lu",
                        t.tx_ok, t.tx_fail, t.rst, t.unicast, (unsigned long)t.up_s);
  n += snprintf(jbuf + n, sizeof(jbuf) - n, ",\"pend\":%d},\"etx\":{\"ok\":%lu,\"fail\":%lu,\"direct\":%d},\"rx\":{\"n\":%lu,\"age\":%lu,\"len\":%d,\"type\":%u,\"ver\":%u,\"magic\":%u,\"exp\":%u},",
                g_ped_pending, (unsigned long)g_tx_ok, (unsigned long)g_tx_fail, (int)g_ped_peer, (unsigned long)g_rx_n, (unsigned long)(g_rx_ms ? millis() - g_rx_ms : 0), g_rx_len,
                g_rx_type, g_rx_ver, g_rx_magic, (unsigned)sizeof(MsgPedTelem));
  n += json_extra(jbuf + n, sizeof(jbuf) - n);            // mise a jour E85 + journal d'incidents
  n += snprintf(jbuf + n, sizeof(jbuf) - n, "\"blind\":%d,\"st\":%d,\"done\":%lu,\"size\":%lu,\"err\":%u}}",
                (int)g_pota_blind, (int)g_pota_state, (unsigned long)g_pota_done, (unsigned long)PEDAL_FW_SIZE, g_pota_err);
  cors();
  server.send(200, "application/json", jbuf);
}

static float argf(const char *k, float def) { return server.hasArg(k) ? server.arg(k).toFloat() : def; }

static void mark_changed() {               // -> copie immediate dans la pedale + flash E85 a l'arret
  S.rev++;
  g_store_ms = 0;
}

static void h_set() {
  const Settings before = S;
  if (server.hasArg("en"))    S.enabled   = server.arg("en").toInt() ? 1 : 0;
  if (server.hasArg("pct"))     S.pct10     = lroundf(argf("pct", 0) * 10);
  for (int i = 0; i < 3; i++) {
    char k[4] = {'t', char('1' + i), 0};
    if (server.hasArg(k)) S.trim10[i] = lroundf(argf(k, 0) * 10);
  }
  if (server.hasArg("dead"))    S.dead_us   = server.arg("dead").toInt();
  if (server.hasArg("maxext"))  S.maxext_us = server.arg("maxext").toInt();
  if (server.hasArg("maxduty")) S.maxduty   = server.arg("maxduty").toInt();
  if (server.hasArg("filt"))    S.filt_us10 = lroundf(argf("filt", 3) * 10);
  if (server.hasArg("inv"))     S.inv       = server.arg("inv").toInt() ? 1 : 0;
  if (server.hasArg("ipc"))     S.ipc       = server.arg("ipc").toInt();
  if (server.hasArg("simrpm"))  S.sim_rpm   = server.arg("simrpm").toInt();
  if (server.hasArg("simpw"))   S.sim_pw_us = server.arg("simpw").toInt();
  apply_settings();
  if (memcmp(&before, &S, sizeof(Settings)) != 0) mark_changed();
  if (server.hasArg("sim")) {
    bool want = server.arg("sim").toInt() != 0;
    // le simulateur est refuse si de vraies impulsions arrivent (moteur tournant)
    if (want && !rt_sim) {
      bool real = false;
      for (int i = 0; i < 3; i++) if (g_last_pulse_ms[i] && millis() - g_last_pulse_ms[i] < 2000) real = true;
      if (!real) {
        g_sim_raw_base = cyl[0].raw_edges + cyl[1].raw_edges + cyl[2].raw_edges;
        rt_sim = 1;
      }
    } else if (!want) rt_sim = 0;
  }
  send_json();
}

static void h_ped() {
  if (!g_telem_valid) { cors(); server.send(503, "application/json", "{\"err\":\"pedale absente\"}"); return; }
  PedConfig c;
  if (g_ped_pending) c = g_ped_des;
  else { portENTER_CRITICAL(&g_mux); c = g_telem.cfg; portEXIT_CRITICAL(&g_mux); }
  if (server.hasArg("aen"))  c.assist_en     = server.arg("aen").toInt() ? 1 : 0;
  if (server.hasArg("tgt"))  c.target_rpm    = constrain(server.arg("tgt").toInt(), 400, 4000);
  if (server.hasArg("tol"))  c.tol_rpm       = constrain(server.arg("tol").toInt(), 0, 500);
  if (server.hasArg("eng"))  c.engage_rpm    = constrain(server.arg("eng").toInt(), 500, 5000);
  if (server.hasArg("off"))  c.off_rpm       = constrain(server.arg("off").toInt(), 800, 7000);
  if (server.hasArg("min"))  c.min_pct10     = constrain(lroundf(argf("min", 0) * 10), 0, 400);
  if (server.hasArg("max"))  c.max_pct10     = constrain(lroundf(argf("max", 0) * 10), 0, 600);
  if (server.hasArg("kp"))   c.kp10          = constrain(lroundf(argf("kp", 0) * 10), 0, 1000);
  if (server.hasArg("ki"))   c.ki10          = constrain(lroundf(argf("ki", 0) * 10), 0, 1000);
  if (server.hasArg("kd"))   c.kd10          = constrain(lroundf(argf("kd", 0) * 10), 0, 1000);
  if (server.hasArg("ant"))  c.anticip_ms    = constrain(server.arg("ant").toInt(), 0, 2000);
  if (server.hasArg("crit")) c.crit_rpm      = constrain(server.arg("crit").toInt(), 0, 3000);
  if (server.hasArg("critp"))c.crit_pct10    = constrain(lroundf(argf("critp", 0) * 10), 0, 400);
  if (server.hasArg("gmin")) c.gmin_pct10    = constrain(lroundf(argf("gmin", 0) * 10), 0, 150);
  if (server.hasArg("rel"))  c.release_pct10 = constrain(lroundf(argf("rel", 3) * 10), 5, 200);
  g_ped_des = c; g_ped_pending = true; g_ped_pending_ms = millis();
  send_ped_cfg(c);
  send_json();
}

static void h_pedcmd() {
  int c = server.arg("c").toInt();
  if (c >= CMD_CAL_RELEASED && c <= CMD_DEFAULTS) send_ped_cmd((uint8_t)c);
  send_json();
}

static void h_save() { send_json(); }       // compatibilite : tout est sauvegarde automatiquement

static void h_defaults() {
  const uint32_t rev = S.rev;
  S = DEFAULTS; S.rev = rev; apply_settings(); mark_changed();
  send_json();
}

static void start_ap();
static void start_espnow();

// ===================== MISE A JOUR DU BOITIER PEDALE =====================
// Le firmware pedale est integre a ce firmware (pedal_fw.h). Il est envoye par
// ESP-NOW, morceau par morceau, avec accuse de reception (reprise automatique).

// envoie un message et attend l'accuse (stage) ; true si recu
static bool pota_xfer(const void *msg, size_t len, uint8_t stage, uint32_t timeout_ms, int retries) {
  for (int k = 0; k < retries; k++) {
    g_ack_new = false;
    tx_ped(msg, len);
    const uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
      if (g_ack_new && g_ack_stage == stage) return true;
      vTaskDelay(1);
    }
  }
  return false;
}

// Envoi "a l'aveugle" : quand la pedale ne peut pas repondre (sa radio n'arrive pas jusqu'au
// boitier E85), chaque morceau est envoye 3 fois, assez lentement pour qu'elle ait le temps
// de l'ecrire. Elle ignore les doublons, et verifie l'empreinte complete du firmware a la fin :
// s'il manque un seul morceau, elle refuse la mise a jour et garde sa version (aucun risque).
static void pota_blind(uint32_t from) {
  g_pota_blind = true;
  Serial.printf("[PED-OTA] pas de reponse -> envoi a l'aveugle depuis %lu\n", (unsigned long)from);
  if (from == 0) {
    MsgOtaBegin b = {}; fill_hdr(b.h, MSG_OTA_BEGIN); b.size = PEDAL_FW_SIZE; b.build = PROJECT_BUILD;
    for (int k = 0; k < 5; k++) { tx_ped(&b, sizeof(b)); vTaskDelay(pdMS_TO_TICKS(300)); }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  uint32_t n = 0;
  for (uint32_t off = from; off < PEDAL_FW_SIZE; off += OTA_CHUNK) {
    MsgOtaData d; memset(&d, 0, sizeof(d)); fill_hdr(d.h, MSG_OTA_DATA);
    d.offset = off; d.len = min<uint32_t>(OTA_CHUNK, PEDAL_FW_SIZE - off);
    memcpy_P(d.data, PEDAL_FW + off, d.len);
    for (int k = 0; k < 3; k++) { tx_ped(&d, sizeof(d)); vTaskDelay(pdMS_TO_TICKS(18)); }
    g_pota_done = off + d.len;
    if (++n % 18 == 0) vTaskDelay(pdMS_TO_TICKS(200));   // la pedale efface un secteur de 4 Ko environ tous les 18 morceaux
  }
  vTaskDelay(pdMS_TO_TICKS(300));
  MsgOtaEnd e = {}; fill_hdr(e.h, MSG_OTA_END); e.size = PEDAL_FW_SIZE;
  for (int k = 0; k < 5; k++) { tx_ped(&e, sizeof(e)); vTaskDelay(pdMS_TO_TICKS(300)); }
}

static void pota_task(void *) {
  g_pota_state = POTA_RUN; g_pota_done = 0; g_pota_err = 0; g_pota_blind = false;
  Serial.printf("[PED-OTA] envoi firmware pedale build %d (%lu octets)\n", PROJECT_BUILD, (unsigned long)PEDAL_FW_SIZE);
  int result = POTA_FAIL;
  do {
    MsgOtaBegin b = {}; fill_hdr(b.h, MSG_OTA_BEGIN); b.size = PEDAL_FW_SIZE; b.build = PROJECT_BUILD;
    if (!pota_xfer(&b, sizeof(b), 1, 800, 4)) { pota_blind(0); result = POTA_SENT; break; }   // aucune reponse
    if (g_ack_status == OTA_SAME) { result = POTA_OK; break; }       // deja a jour : rien a envoyer
    if (g_ack_status != OTA_OK) { g_pota_err = 1; break; }
    uint32_t off = 0; int fails = 0; bool blind = false;
    while (off < PEDAL_FW_SIZE) {
      MsgOtaData d; memset(&d, 0, sizeof(d)); fill_hdr(d.h, MSG_OTA_DATA);
      d.offset = off; d.len = min<uint32_t>(OTA_CHUNK, PEDAL_FW_SIZE - off);
      memcpy_P(d.data, PEDAL_FW + off, d.len);
      if (!pota_xfer(&d, sizeof(d), 2, 300, 1) || g_ack_status != OTA_OK) {
        if (++fails > 15) { pota_blind(off); blind = true; break; }   // les reponses ne passent plus
        continue;
      }
      fails = 0;
      off = g_ack_next;                                  // la pedale dit ou reprendre
      g_pota_done = off;
    }
    if (blind) { result = POTA_SENT; break; }
    MsgOtaEnd e = {}; fill_hdr(e.h, MSG_OTA_END); e.size = PEDAL_FW_SIZE;
    if (!pota_xfer(&e, sizeof(e), 3, 1500, 5)) { result = POTA_SENT; break; }   // fin envoyee, pas de confirmation
    if (g_ack_status != OTA_OK) { g_pota_err = 3; break; }
    result = POTA_OK;
  } while (0);
  if (result != POTA_FAIL && g_ack_status == OTA_SAME) Serial.println("[PED-OTA] la pedale etait deja a jour");
  g_pota_end_ms = millis();                // les decisions suivantes attendront un message NEUF de la pedale
  g_pota_state = result;
  Serial.printf("[PED-OTA] %s (err=%u)\n", result == POTA_OK ? "succes" : result == POTA_SENT ? "envoye sans confirmation" : "ECHEC", g_pota_err);
  vTaskDelete(nullptr);
}
static bool pota_start(bool force = false) {
  if (g_pota_state == POTA_RUN || g_e85_ota) return false;
  if (!force && !ped_link()) return false;
  g_pota_last_try = millis(); g_pota_tries++;
  xTaskCreatePinnedToCore(pota_task, "pota", 6144, nullptr, 2, nullptr, 0);
  return true;
}

// Manuel (bouton de l'appli) : autorise moteur tournant. La pedale continue de fonctionner
// pendant le transfert et ne redemarre qu'une fois le pied leve.
static void h_pedota() {
  const bool force = server.arg("force").toInt() != 0;   // envoi meme si la pedale n'est pas detectee
  if (!pota_start(force)) { cors(); server.send(409, "application/json", "{\"err\":\"Pedale absente ou mise a jour deja en cours\"}"); return; }
  send_json();
}

// ===================== MISE A JOUR DU BOITIER E85 (depuis l'appli) =====================
// En DEUX TEMPS, pour pouvoir la lancer moteur tournant sans jamais toucher a l'injection :
//  1. reception du firmware dans la PSRAM (RAM externe) : aucune ecriture flash ;
//  2. installation en flash des que le moteur est arrete depuis 3 s (contact mis ~20 s).
//     Si le moteur redemarre pendant l'installation, elle se met en pause et reprend.
// Securite : ecriture dans l'emplacement libre, empreinte verifiee avant de basculer,
// et le nouveau programme doit prouver qu'il fonctionne (sinon retour automatique a l'ancien).
enum { E85U_NONE = 0, E85U_RX = 1, E85U_READY = 2, E85U_INSTALL = 3, E85U_DONE = 4, E85U_FAIL = 5 };
static volatile int    g_e85u = E85U_NONE;
static volatile size_t g_e85u_done = 0;
static uint8_t *g_fw_buf = nullptr;
static size_t   g_fw_len = 0;
static const size_t FW_MAX = 0x300000;        // taille d'un emplacement programme (3 Mo)
static String   g_e85u_err;

static void h_ota_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_e85u_err = "";
    if (g_pota_state == POTA_RUN || g_e85u == E85U_INSTALL) { g_e85u_err = "Une mise a jour est deja en cours"; return; }
    if (!g_fw_buf) g_fw_buf = (uint8_t *)heap_caps_malloc(FW_MAX, MALLOC_CAP_SPIRAM);
    if (!g_fw_buf) { g_e85u_err = "PSRAM indisponible"; return; }
    g_fw_len = 0; g_e85u = E85U_RX; g_e85u_done = 0; g_e85_ota = true;
    Serial.printf("[OTA] reception en PSRAM : %s\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_e85u != E85U_RX) return;
    if (g_fw_len + up.currentSize > FW_MAX) { g_e85u_err = "Fichier trop gros"; g_e85u = E85U_FAIL; return; }
    memcpy(g_fw_buf + g_fw_len, up.buf, up.currentSize);
    if (((g_fw_len + up.currentSize) >> 15) != (g_fw_len >> 15)) vTaskDelay(1);   // laisse respirer le coeur 0 tous les 32 Ko
    g_fw_len += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_e85u != E85U_RX) return;
    // controle immediat de l'image (en-tete ESP32-S3) ; l'empreinte complete est verifiee a l'installation
    if (g_fw_len < 100000 || g_fw_buf[0] != 0xE9) { g_e85u_err = "Fichier invalide"; g_e85u = E85U_FAIL; g_e85_ota = false; return; }
    g_e85u = E85U_READY; g_e85_ota = false;
    Serial.printf("[OTA] %u octets recus, installation a l'arret du moteur\n", (unsigned)g_fw_len);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    g_e85u = E85U_NONE; g_e85_ota = false;
  }
}
static void h_ota_done() {
  cors();
  if (g_e85u != E85U_READY) {
    String e = g_e85u_err.length() ? g_e85u_err : String("Reception incomplete");
    server.send(500, "application/json", "{\"err\":\"" + e + "\"}");
    if (g_e85u != E85U_INSTALL) g_e85u = E85U_NONE;
    return;
  }
  server.send(200, "application/json", g_engine_on ? "{\"ok\":1,\"pending\":1}" : "{\"ok\":1,\"pending\":0}");
}

// appele a chaque tour de la tache reseau : installe par blocs de 4 Ko, seulement moteur arrete
static void e85u_step(uint32_t ms) {
  if (g_e85u != E85U_READY && g_e85u != E85U_INSTALL) return;
  const bool safe = !g_engine_on && !rt_sim && ms - g_engine_off_since > 3000;
  if (!safe) return;                                     // moteur tournant : on attend (ou on met en pause)
  if (g_e85u == E85U_READY) {
    if (!Update.begin(g_fw_len)) { g_e85u_err = Update.errorString(); g_e85u = E85U_FAIL; return; }
    g_e85u = E85U_INSTALL; g_e85u_done = 0; g_e85_ota = true;
    Serial.println("[OTA] installation...");
  }
  const size_t n = min<size_t>(4096, g_fw_len - g_e85u_done);
  if (Update.write(g_fw_buf + g_e85u_done, n) != n) { g_e85u_err = Update.errorString(); Update.abort(); g_e85u = E85U_FAIL; g_e85_ota = false; return; }
  g_e85u_done += n;
  if (g_e85u_done >= g_fw_len) {
    if (!Update.end(true)) { g_e85u_err = Update.errorString(); g_e85u = E85U_FAIL; g_e85_ota = false; return; }
    g_e85u = E85U_DONE;
    Serial.println("[OTA] installe et verifie -> redemarrage");
  }
}
// ============================ MOT DE PASSE WI-FI ============================
static void h_wifi() {
  cors();
  String p = server.arg("pass");
  if (p.length() < 8 || p.length() > 63) { server.send(400, "application/json", "{\"err\":\"8 caracteres minimum\"}"); return; }
  if (g_engine_on) { server.send(409, "application/json", "{\"err\":\"Arrete le moteur pour changer le mot de passe\"}"); return; }
  prefs.begin("e85", false); prefs.putString("pass", p); prefs.end();
  server.send(200, "application/json", "{\"ok\":1}");
  delay(300);
  AP_PASS = p; start_ap(); start_espnow();
}

// ============================ DIAGNOSTIC ============================
// "Fil d'Ariane" en RAM RTC : survit a tout redemarrage logiciel, plantage, chien de garde
// ou chute d'alimentation (pas a une coupure de courant). Au demarrage suivant, il est
// recopie en flash -> l'appli montre l'etat exact du boitier juste avant l'incident.
#define CRUMB_MAGIC 0xC0FFEE85u
struct Crumb {
  uint32_t magic, up_s, heap_min, stage, clients, rx_n, tx_fail, pota_state, pota_done, rpm, hang;
};
RTC_NOINIT_ATTR static Crumb g_crumb;
struct DiagStore { uint16_t cnt[12]; uint8_t last_reason; uint8_t has_last; Crumb last; };
static DiagStore g_diag;
static volatile uint32_t g_comm_beat = 0;      // derniere iteration de la tache reseau

// appele dans setup(), AVANT le demarrage du temps reel (ecriture flash autorisee)
static void diag_boot() {
  memset(&g_diag, 0, sizeof(g_diag));
  prefs.begin("diag", false);
  if (prefs.getBytesLength("d") == sizeof(DiagStore)) prefs.getBytes("d", &g_diag, sizeof(DiagStore));
  const int r = (int)esp_reset_reason();
  if (r >= 0 && r < 12) g_diag.cnt[r]++;
  if (g_crumb.magic == CRUMB_MAGIC && r != ESP_RST_POWERON) {
    g_diag.last = g_crumb; g_diag.last_reason = (uint8_t)r; g_diag.has_last = 1;
  }
  prefs.putBytes("d", &g_diag, sizeof(DiagStore));
  prefs.end();
  memset(&g_crumb, 0, sizeof(g_crumb));
  g_crumb.magic = CRUMB_MAGIC;
}

// Surveillance : si la tache reseau ne tourne plus depuis 6 s, le Wi-Fi est bloque.
// Moteur arrete -> redemarrage immediat. Moteur tournant -> on attend l'arret
// (un redemarrage couperait l'injection ; le coeur temps reel, lui, continue).
static void monitor_task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    g_crumb.up_s = millis() / 1000;
    g_crumb.heap_min = esp_get_minimum_free_heap_size();
    g_crumb.rpm = g_rpm;
    if (g_comm_beat && millis() - g_comm_beat > 6000) {
      g_crumb.hang = 1;
      if (!g_engine_on) {
        Serial.println("[DIAG] tache reseau bloquee -> redemarrage");
        delay(50);
        esp_restart();
      }
    }
  }
}

// morceau de JSON : etat de la mise a jour E85 + journal d'incidents, termine par "pota":{
static int json_extra(char *b, size_t sz) {
  int n = snprintf(b, sz, "\"e85u\":{\"st\":%d,\"done\":%u,\"size\":%u,\"err\":\"%s\",\"psram\":%u},",
                   (int)g_e85u, (unsigned)g_e85u_done, (unsigned)g_fw_len, g_e85u_err.c_str(),
                   (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
  n += snprintf(b + n, sz - n, "\"diag\":{\"cnt\":[");
  for (int i = 0; i < 12; i++) n += snprintf(b + n, sz - n, "%s%u", i ? "," : "", g_diag.cnt[i]);
  n += snprintf(b + n, sz - n, "]");
  if (g_diag.has_last) {
    const Crumb &c = g_diag.last;
    n += snprintf(b + n, sz - n, ",\"last\":{\"r\":%u,\"up\":%lu,\"stage\":%lu,\"heap\":%lu,\"cl\":%lu,\"rx\":%lu,\"txf\":%lu,\"pst\":%lu,\"pdone\":%lu,\"rpm\":%lu,\"hang\":%lu}",
                  g_diag.last_reason, (unsigned long)c.up_s, (unsigned long)c.stage, (unsigned long)c.heap_min, (unsigned long)c.clients,
                  (unsigned long)c.rx_n, (unsigned long)c.tx_fail, (unsigned long)c.pota_state, (unsigned long)c.pota_done,
                  (unsigned long)c.rpm, (unsigned long)c.hang);
  }
  n += snprintf(b + n, sz - n, "},\"pota\":{");
  return n;
}

// ========================= TACHE COMMUNICATION =========================
static void start_ap() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS.c_str(), LINK_CHANNEL, 0, 4);
  // 13 dBm : largement assez dans une voiture, et chaque emission consomme moins
  // (les pics de courant du Wi-Fi font chuter une alimentation un peu juste)
  WiFi.setTxPower(WIFI_POWER_13dBm);
}

static void start_espnow() {
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) { Serial.println("[ESPNOW] init echec"); return; }
  esp_now_register_recv_cb(on_recv);
  esp_now_register_send_cb(on_sent);
  if (g_ped_peer) { g_ped_peer = false; g_ped_mac_new = true; }   // le pair direct sera re-ajoute
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BCAST, 6);
  p.channel = LINK_CHANNEL; p.ifidx = WIFI_IF_AP; p.encrypt = false;
  esp_now_add_peer(&p);
}

static void comm_task(void *) {
  Serial.begin(115200);
  // RX du port serie (GPIO44) tire au niveau haut : quand l'USB est debranche, la puce USB n'est
  // plus alimentee et sa sortie flotte -> sans ca, parasites et interruptions inutiles sur le coeur 0.
  gpio_pullup_en(GPIO_NUM_44);
  delay(200);
  Serial.printf("\n[BOOT] Boitier E85 v%s - reset=%d - CPU %lu MHz\n", FW_VERSION, (int)esp_reset_reason(), (unsigned long)getCpuFrequencyMhz());
  Serial.printf("[BOOT] PSRAM %u Ko - incidents : alim=%u plantage=%u chien_de_garde=%u\n",
                (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024), g_diag.cnt[ESP_RST_BROWNOUT],
                g_diag.cnt[ESP_RST_PANIC], g_diag.cnt[ESP_RST_TASK_WDT] + g_diag.cnt[ESP_RST_INT_WDT] + g_diag.cnt[ESP_RST_WDT]);
#ifdef TEST_CRASH   // uniquement pour tester le retour arriere automatique (jamais dans une version publiee)
  delay(5000); Serial.println("[TEST] plantage volontaire"); abort();
#endif
  start_ap();
  start_espnow();
  Serial.printf("[WiFi] AP %s  ip=%s  canal=%d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), LINK_CHANNEL);

  server.on("/", []() { cors(); server.send(200, "text/html; charset=utf-8", WEB_UI); });
  server.on("/api/status",   send_json);
  server.on("/api/set",      h_set);
  server.on("/api/ped",      h_ped);
  server.on("/api/pedcmd",   h_pedcmd);
  server.on("/api/save",     h_save);
  server.on("/api/defaults", h_defaults);
  server.on("/api/pedota",   h_pedota);
  server.on("/api/wifi",     h_wifi);
  server.on("/api/ota", HTTP_POST, h_ota_done, h_ota_upload);
  server.onNotFound([]() { cors(); server.send(404, "text/plain", "404"); });
  server.begin();

  uint32_t t_fast = 0, t_cfg = 0, t_log = 0, t_wd = 0;
  for (;;) {
    g_comm_beat = millis();
    g_crumb.stage = 1;
    server.handleClient();
    g_crumb.stage = 2;
    g_crumb.clients = WiFi.softAPgetStationNum();
    g_crumb.rx_n = g_rx_n; g_crumb.tx_fail = g_tx_fail;
    g_crumb.pota_state = g_pota_state; g_crumb.pota_done = g_pota_done;
    const uint32_t ms = millis();

    if (ms - t_fast >= 20) {                 // 50 Hz : regime + derivee vers la pedale
      t_fast = ms;
      update_engine();
      send_status();
      if (rt_sim) {                          // vraies impulsions pendant la simulation -> on arrete
        uint32_t re = cyl[0].raw_edges + cyl[1].raw_edges + cyl[2].raw_edges;
        if (re - g_sim_raw_base > 20) { rt_sim = 0; Serial.println("[SIM] ECU reelle detectee -> simulateur arrete"); }
      }
    }

    if (g_ped_pending && ms - t_cfg >= 150) { // renvoi de la config pedale jusqu'a confirmation
      t_cfg = ms;
      PedConfig cur; portENTER_CRITICAL(&g_mux); cur = g_telem.cfg; portEXIT_CRITICAL(&g_mux);
      if (memcmp(&cur, &g_ped_des, sizeof(PedConfig)) == 0 || ms - g_ped_pending_ms > 4000) g_ped_pending = false;
      else send_ped_cfg(g_ped_des);
    }

    // --- adresse de la pedale apprise -> envoi direct ---
    if (g_ped_mac_new) {
      uint8_t mac[6]; portENTER_CRITICAL(&g_mux); memcpy(mac, g_ped_mac, 6); g_ped_mac_new = false; portEXIT_CRITICAL(&g_mux);
      esp_now_peer_info_t p = {};
      memcpy(p.peer_addr, mac, 6); p.channel = LINK_CHANNEL; p.ifidx = WIFI_IF_AP; p.encrypt = false;
      if (esp_now_is_peer_exist(mac) || esp_now_add_peer(&p) == ESP_OK) {
        g_ped_peer = true;
        Serial.printf("[ESPNOW] pedale %02X:%02X:%02X:%02X:%02X:%02X -> envoi direct\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      }
    }

    // --- reglages recuperes depuis la pedale (au demarrage, si sa copie est plus recente) ---
    if (g_restore_new) {
      E85Settings r; portENTER_CRITICAL(&g_mux); r = g_restore; g_restore_new = false; portEXIT_CRITICAL(&g_mux);
      if (r.rev > S.rev) {
        S = r; apply_settings();
        Serial.printf("[SYNC] reglages restaures depuis la pedale (rev %lu)\n", (unsigned long)S.rev);
      }
    }

    // --- synchronisation avec la memoire du boitier pedale ---
    if (ped_link() && ms - g_sync_ms >= 250) {
      g_sync_ms = ms;
      uint32_t prev; portENTER_CRITICAL(&g_mux); prev = g_telem.e85_rev; portEXIT_CRITICAL(&g_mux);
      if (prev > S.rev) {
        send_ped_cmd(CMD_SEND_E85);                       // la pedale a plus recent -> on le recupere
      } else if (prev != S.rev && ms - g_store_ms >= 250) {
        g_store_ms = ms; send_store();                    // on lui envoie notre version
      }
    }

    // Ecriture flash E85 UNIQUEMENT moteur arrete (une ecriture flash gele le coeur temps reel).
    // Moteur tournant, la copie est deja en securite dans la pedale.
    if (g_saved_rev != S.rev && !g_engine_on && !rt_sim && ms - g_engine_off_since > 1500) {
      write_settings();
      Serial.printf("[NVS] reglages sauvegardes (rev %lu)\n", (unsigned long)S.rev);
    }

    // --- installation de la mise a jour du boitier E85 (moteur arrete seulement) ---
    e85u_step(ms);
    static uint32_t done_ms = 0;
    if (g_e85u == E85U_DONE) { if (!done_ms) done_ms = ms; else if (ms - done_ms > 1500) ESP.restart(); }

    // --- le nouveau programme se valide lui-meme apres 20 s de bon fonctionnement ---
    // (s'il plante ou se bloque avant, le demarreur revient tout seul a l'ancienne version)
    static bool validated = false;
    if (!validated && ms > 20000 && WiFi.getMode() == WIFI_AP) {
      validated = true;
      esp_ota_img_states_t st;
      if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] nouvelle version validee");
      }
    }

    // --- mise a jour automatique de la pedale si elle n'a pas la meme version ---
    // Moteur tournant : seulement si la pedale sait attendre le pied leve pour redemarrer (build >= 231).
    if (ped_link() && g_pota_state != POTA_RUN && !g_e85_ota && g_e85u != E85U_INSTALL && !rt_sim
        && ms > 15000 && g_pota_tries < 3 && (!g_pota_last_try || ms - g_pota_last_try > 60000)
        // apres une mise a jour : 30 s de delai ET un message recu APRES le redemarrage de la pedale
        // (sinon on lirait son dernier message d'avant redemarrage, avec l'ancienne version)
        && (!g_pota_end_ms || (ms - g_pota_end_ms > 30000 && (int32_t)(g_telem_ms - g_pota_end_ms) > 5000))) {
      uint16_t pb, pp; portENTER_CRITICAL(&g_mux); pb = g_telem.fw_build; pp = g_telem.pedal_pct10; portEXIT_CRITICAL(&g_mux);
      const bool ok_now = !g_engine_on ? (ms - g_engine_off_since > 5000 && pp < 50) : (pb >= 231);
      if (pb && pb != PROJECT_BUILD && ok_now) {
        Serial.printf("[PED-OTA] pedale en build %u, attendu %d -> mise a jour\n", pb, PROJECT_BUILD);
        pota_start();
      }
    }

    if (ms - t_wd >= 5000 && !g_e85_ota) {   // surveillance du point d'acces
      t_wd = ms;
      if (WiFi.getMode() != WIFI_AP || WiFi.softAPIP()[0] == 0) {
        Serial.println("[WiFi] AP anormal -> relance");
        start_ap(); start_espnow();
      }
    }

    if (ms - t_log >= 2000) {
      t_log = ms;
      Serial.printf("[E85] rpm=%u on=%d ext=%.1f%% pw1=%luus ext1=%luus ped=%d sim=%d\n",
        g_rpm, g_engine_on, S.pct10 / 10.0f, (unsigned long)(cyl[0].width_t / TPU), (unsigned long)(cyl[0].ext_t / TPU),
        ped_link(), (int)rt_sim);
    }
    vTaskDelay(1);
  }
}

// ================================ SETUP ================================
void setup() {
  // 1) sorties a l'etat bas le plus tot possible (injecteurs fermes)
  for (int i = 0; i < 3; i++) {
    gpio_reset_pin((gpio_num_t)PIN_OUT[i]);
    gpio_set_level((gpio_num_t)PIN_OUT[i], 0);
    gpio_set_direction((gpio_num_t)PIN_OUT[i], GPIO_MODE_OUTPUT);
    gpio_set_drive_capability((gpio_num_t)PIN_OUT[i], GPIO_DRIVE_CAP_3);
    gpio_reset_pin((gpio_num_t)PIN_IN[i]);
    gpio_set_direction((gpio_num_t)PIN_IN[i], GPIO_MODE_INPUT);
    gpio_pulldown_en((gpio_num_t)PIN_IN[i]);
    gpio_pullup_dis((gpio_num_t)PIN_IN[i]);
  }

  TPU = getCpuFrequencyMhz();                 // 240
  PERIOD_MIN_T = 2500UL * TPU;                // 2,5 ms  (> 9000 tr/min en sequentiel)
  PERIOD_MAX_T = 1000000UL * TPU;             // 1 s     (< 120 tr/min)

  load_settings();                            // flash lue AVANT le demarrage temps reel
  diag_boot();                                // compteurs d'incidents (ecriture flash, avant le temps reel)
  apply_settings();

  for (int i = 0; i < 3; i++) {
    Cyl &c = cyl[i];
    memset((void *)&c, 0, sizeof(Cyl));
    c.in_shift = PIN_IN[i];
    c.out_mask = 1UL << PIN_OUT[i];
    c.raw_last = (REG_READ(GPIO_IN_REG) >> c.in_shift) & 1u;
    c.stable   = c.raw_last ^ rt_inv;
  }

  // 2) tout le reseau sur le coeur 0
  xTaskCreatePinnedToCore(comm_task, "comm", 8192, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(monitor_task, "mon", 3072, nullptr, 10, nullptr, 0);

  // 3) le coeur 1 (loopTask) devient la boucle temps reel - ne revient jamais
  disableCore1WDT();
  delay(50);
  rt_loop();
}

void loop() { /* jamais atteint */ }
