// =====================================================================
//  protocol.h  -  Protocole ESP-NOW commun Boitier E85 <-> Boitier Pedale
//  !! Ce fichier doit etre IDENTIQUE dans E85_Master/ et Pedale/ !!
// =====================================================================
#pragma once
#include <stdint.h>

// Version du projet : E85, pedale et appli sont publies ensemble avec ce numero.
#define PROJECT_VERSION "2.3.1"
#define PROJECT_BUILD   231         // entier croissant (2.3.1 -> 231)

#define LINK_MAGIC      0xE85Au     // identifiant du reseau (change-le si tu as 2 voitures)
#define LINK_VERSION    2
#define LINK_CHANNEL    1           // canal Wi-Fi commun (AP du boitier E85 + ESP-NOW)

enum : uint8_t {
  MSG_E85_STATUS   = 1,   // E85 -> Pedale : regime + derivee (50 Hz)
  MSG_PED_CONFIG   = 2,   // E85 -> Pedale : reglages pedale
  MSG_PED_CMD      = 3,   // E85 -> Pedale : commande ponctuelle
  MSG_E85_STORE    = 4,   // E85 -> Pedale : copie des reglages E85 a memoriser
  MSG_PED_TELEM    = 10,  // Pedale -> E85 : mesures (20 Hz)
  MSG_E85_RESTORE  = 11,  // Pedale -> E85 : reglages E85 memorises
  MSG_OTA_BEGIN    = 20,  // E85 -> Pedale : debut mise a jour sans fil
  MSG_OTA_DATA     = 21,  // E85 -> Pedale : morceau de firmware
  MSG_OTA_END      = 22,  // E85 -> Pedale : fin -> verification + redemarrage
  MSG_OTA_ACK      = 30,  // Pedale -> E85 : accuse de reception
};

#define OTA_CHUNK 224

enum : uint8_t { OTA_OK = 0, OTA_ERR_BEGIN = 1, OTA_ERR_WRITE = 2, OTA_ERR_VERIFY = 3, OTA_ERR_BUSY = 4 };

enum : uint8_t {
  CMD_CAL_RELEASED = 1, // memoriser tensions pedale relachee
  CMD_CAL_FLOORED  = 2, // memoriser tensions pedale a fond
  CMD_SAVE         = 3, // (compatibilite) sauvegarde immediate
  CMD_DEFAULTS     = 4, // reglages pedale usine
  CMD_SEND_E85     = 5, // renvoyer la copie des reglages E85
};

#pragma pack(push, 1)
struct LinkHeader {
  uint16_t magic;
  uint8_t  version;
  uint8_t  type;
  uint16_t seq;
};

// Reglages du boitier E85 (aussi memorises dans le boitier pedale)
struct E85Settings {
  uint32_t rev;          // compteur de modifications (le plus grand gagne)
  uint8_t  enabled;      // 1 = prolongement actif
  uint8_t  inv;          // 1 = entrees inversees
  uint8_t  ipc;          // 1 = sequentiel, 2 = semi-sequentiel
  uint8_t  reserved;
  int16_t  pct10;        // prolongement global (0,1 %)
  int16_t  trim10[3];    // correction par cylindre (0,1 %)
  uint16_t dead_us;      // temps mort injecteur (us)
  uint16_t maxext_us;    // prolongement maxi (us)
  uint8_t  maxduty;      // rapport cyclique maxi (%)
  uint8_t  filt_us10;    // filtre entrees (0,1 us)
  uint16_t sim_rpm;
  uint16_t sim_pw_us;
};

struct MsgE85Status {
  LinkHeader h;
  uint16_t rpm;          // 0 = moteur arrete
  int16_t  rpm_rate;     // variation du regime (tr/min par seconde, filtree)
  uint8_t  engine_on;
  uint8_t  reserved;
};

struct PedConfig {
  uint8_t  assist_en;     // 1 = aide au ralenti active
  uint8_t  reserved;
  uint16_t target_rpm;    // regime vise pied leve
  uint16_t tol_rpm;       // tolerance +- autour du regime vise (integrale figee dedans)
  uint16_t engage_rpm;    // pied leve : l'aide s'enclenche sous ce regime (ou s'il y sera bientot)
  uint16_t off_rpm;       // pied leve : au-dessus, l'aide se coupe (frein moteur / coupure injection)
  uint16_t min_pct10;     // plancher d'ouverture des que l'aide est enclenchee (0,1 %)
  uint16_t max_pct10;     // ouverture maxi (0,1 %)
  uint16_t kp10;          // P : % par 100 tr/min d'ecart (x10)
  uint16_t ki10;          // I : % par 100 tr/min par seconde (x10)
  uint16_t kd10;          // anticipation : % par (100 tr/min/s) de chute (x10)
  uint16_t anticip_ms;    // horizon de prediction du regime (ms)
  uint16_t release_pct10; // pedale consideree relachee sous ce seuil (0,1 %)
  // --- ajoute en 2.2.0 (toujours EN FIN de structure : compatibilite avec les anciennes versions) ---
  uint16_t crit_rpm;      // seuil critique : sous ce regime, ouverture forcee immediate (0 = desactive)
  uint16_t crit_pct10;    // ouverture forcee sous le seuil critique (0,1 %)
  // --- ajoute en 2.3.0 ---
  uint16_t gmin_pct10;    // ouverture minimale GENERALE de la pedale, avec ou sans aide (0,1 %, 0 = desactive)
};
// taille de PedConfig avant la 2.2.0 (pour accepter les messages des anciennes versions)
#define PEDCONFIG_V21_SIZE 24

struct MsgPedConfig { LinkHeader h; PedConfig cfg; };
struct MsgPedCmd    { LinkHeader h; uint8_t cmd; uint8_t arg; };
struct MsgE85Store  { LinkHeader h; E85Settings s; };

struct MsgPedTelem {
  LinkHeader h;
  uint16_t in1_mv, in2_mv;     // tensions lues sur la vraie pedale
  uint16_t out1_mv, out2_mv;   // tensions envoyees au calculateur
  uint16_t vref_mv;
  uint16_t pedal_pct10;        // position pedale conducteur (0,1 %)
  uint16_t out_pct10;          // position envoyee (0,1 %)
  uint16_t assist_pct10;       // ouverture de l'aide (0,1 %)
  uint16_t learned_pct10;      // ouverture apprise (integrale) (0,1 %)
  uint8_t  mode;               // 0=bypass 1=recopie 2=aide 3=defaut
  uint8_t  flags;              // b0 calibre, b1 lien, b2 ecriture en cours, b3 I2C ok, b4 aide enclenchee
  uint16_t cal_rel1, cal_rel2, cal_full1, cal_full2;
  uint32_t e85_rev;            // revision des reglages E85 memorises (0 = aucun)
  uint16_t fw_build;           // PROJECT_BUILD du firmware pedale
  PedConfig cfg;
};

struct MsgOtaBegin { LinkHeader h; uint32_t size; uint16_t build; };
struct MsgOtaData  { LinkHeader h; uint32_t offset; uint16_t len; uint8_t data[OTA_CHUNK]; };
struct MsgOtaEnd   { LinkHeader h; uint32_t size; };
struct MsgOtaAck   { LinkHeader h; uint8_t stage; uint8_t status; uint32_t next; };  // stage 1=begin 2=data 3=end
#pragma pack(pop)
