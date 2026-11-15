# Boîtier E85 v2.0 : notice complète

Trois éléments :

| Élément | Rôle | Fichier |
|---|---|---|
| Boîtier E85 (ESP32-S3) | Lit les 3 injecteurs, prolonge l'injection, calcule le régime, point d'accès Wi-Fi `BoitierE85` | `firmware/E85_Master` |
| Boîtier pédale (ESP32-S3) | Recopie / corrige les 2 pistes de la pédale selon le régime | `firmware/Pedale` |
| Appli Android | Tableau de bord + réglages | `livrables/BoitierE85.apk` |

```
 Téléphone ──Wi-Fi (192.168.4.1)──► Boîtier E85 ◄──ESP-NOW (canal 1)──► Boîtier pédale
                                      ▲   │                                ▲     │
                           ECU inj 1-3│   │MOSFET → injecteurs     pédale  │     │ DAC → ECU
```

L'appli ne parle qu'au boîtier E85 : c'est lui qui relaie vers la pédale en ESP-NOW (un téléphone ne sait pas faire de l'ESP-NOW). Le boîtier sert aussi la même interface sur `http://192.168.4.1` dans n'importe quel navigateur.

---

## 1. Comment fonctionne la précision du prolongement

* Le **cœur 1** de l'ESP32-S3 ne fait qu'une chose : une boucle en mémoire IRAM, sans interruption ni appel système. Elle lit en continu le registre des GPIO et le compteur de cycles CPU (240 MHz, soit 4,2 ns de résolution). Un tour complet pour les 3 cylindres prend environ 0,1 µs. Vérifié sur le binaire compilé : 199 instructions, aucun appel de fonction.
* **Front montant ECU** : la sortie s'active immédiatement (retard = filtre anti-parasite, 3 µs par défaut).
* **Front descendant ECU** : on mesure la durée exacte, on calcule `prolongement = (durée − temps mort) × %` et la sortie est coupée pile à l'instant voulu.
* Le Wi-Fi, la page web et l'ESP-NOW tournent sur le **cœur 0** et ne peuvent pas perturber le cœur 1.
* Une écriture en flash gèle les deux cœurs pendant quelques ms. Les sauvegardes sont donc **différées tant que le moteur tourne** (bandeau « Sauvegarde en attente » dans l'appli).
* Sécurités : prolongement maxi (15 ms par défaut) et rapport cyclique maxi (90 %). L'injecteur n'est jamais ouvert en continu.
* Le plus gros retard restant vient du PC817 (quelques µs à l'ouverture comme à la fermeture). Il se compense presque entièrement, puisque les deux fronts sont retardés pareil.

### Le « temps mort injecteur »
Un injecteur met environ 0,7 à 1 ms à s'ouvrir, et pendant ce temps il n'injecte rien. Avec temps mort = 0 (défaut), +25 % allonge l'impulsion totale de 25 %, comme ton ancien boîtier. Si tu règles le temps mort (par ex. 900 µs), +25 % donne vraiment +25 % de carburant, surtout au ralenti où les impulsions sont courtes. Commence à 0, puis passe à 800–900 µs si le mélange au ralenti est trop riche par rapport au reste.

---

## 2. Câblage boîtier E85

### Entrées (×3) : signal ECU → PC817 → GPIO 4 / 5 / 6

```
 +12V (après contact) ──[ R1 1 kΩ ½ W ]──┬──► PC817 broche 1 (anode)
                                         │
                                 1N4148 (cathode vers +12V, anode vers broche 2)
                                         │
 Fil ECU injecteur n ────────────────────┴──► PC817 broche 2 (cathode)

 +3,3V ESP ──► PC817 broche 4 (collecteur)
 PC817 broche 3 (émetteur) ──┬──► GPIO 4 / 5 / 6
                             └──[ R2 2,2 kΩ ]── GND ESP
```
* L'ECU tire son fil à la masse pour injecter → la LED s'allume → GPIO = 3,3 V (niveau haut = injection, comme ton montage actuel).
* La 1N4148 protège la LED du PC817 contre la surtension de l'ECU à la coupure.
* R2 entre 1 et 2,2 kΩ : plus elle est faible, plus le PC817 est rapide. Ne dépasse pas 4,7 kΩ.
* **L'ECU ne voit plus le vrai injecteur.** Certaines ECU allument le voyant moteur (« circuit injecteur ouvert »). Dans ce cas, ajoute en parallèle sur chaque fil ECU une résistance de charge vers +12 V, de 15 à 22 Ω et 25 W minimum sur dissipateur (ou un vieil injecteur). Si ton ancien montage ne déclenchait pas de défaut, garde-le tel quel.

### Sorties (×3) : GPIO 7 / 15 / 16 → IRLB8721 → injecteur

```
 GPIO 7/15/16 ──[ 100 Ω ]──┬──► Gate IRLB8721
                           └──[ 10 kΩ ]── Source
 Source ──► GND puissance (fil de masse direct au bloc / batterie, PAS via l'ESP)

 +12V ──► injecteur ──► Drain IRLB8721
            │                  │
            └── écrêteur ──────┘   (voir ci-dessous, OBLIGATOIRE)
```

### ⚠️ Écrêteur de surtension injecteur, sans doute la cause de tes plantages

À la fermeture, un injecteur renvoie une surtension de 60 à 100 V. L'IRLB8721 ne tient que **30 V**. Sans écrêteur, le MOSFET part en avalanche à chaque fermeture. Ça injecte des parasites dans la masse et l'alim, ce qui fait redémarrer l'ESP (ton ancien boîtier comptait 54 brownouts et 66 plantages). À terme, le MOSFET meurt en court-circuit, injecteur ouvert en permanence.

Montage par injecteur, entre le **drain** et le **+12 V** :
```
 Drain ──►|── UF4007 ──|◄── Zener 12 V 1,3 W (1N4742A) ── +12V
         (anode côté drain)    (les deux cathodes face à face)
```
La tension maxi sur le drain vaut 14,4 + 12 + 0,7 ≈ 27 V, sous les 30 V de l'IRLB8721. La fermeture reste rapide, contrairement à une simple diode de roue libre qui retarderait la fermeture de l'injecteur d'environ 1 ms.

Alternative plus robuste : un MOSFET 60 V logique, comme l'IRLZ44N, avec une zener de 33 V.

### Alimentation (l'autre cause des brownouts)
```
 +12V contact ── fusible 2 A ── diode SS34 (anti-inversion) ──┬── TVS SMBJ24A ── GND
                                                              └── convertisseur buck 12→5 V auto (MP1584 / LM2596)
 5V ── 470 µF + 100 nF ── broche 5V de la carte ESP32-S3
```
* Les masses de puissance (sources des MOSFET) et la masse de l'ESP se rejoignent en **un seul point** (masse en étoile).
* Ne jamais alimenter l'ESP via le régulateur 3,3 V de la carte depuis le 12 V.

---

## 3. Câblage boîtier pédale

### Ton montage actuel (relevé sur le firmware d'origine v1.3.0) : c'est celui que gère le firmware v2.0
| Fonction | Branchement |
|---|---|
| I2C | SDA = GPIO8, SCL = GPIO9 (400 kHz) |
| ADS1115 (0x48) | **A2 = piste 1** pédale, **A3 = piste 2** (lecture directe, sans pont, plage ±6,144 V) |
| MCP4725 **0x61** | sortie **piste 1** vers ECU |
| MCP4725 **0x60** | sortie **piste 2** vers ECU |
| Alimentation MCP4725 | 5 V (la sortie suit cette tension : réglable avec `VREF_MV` dans le code) |

Calibration d'origine reprise par défaut : piste 1 de 0,396 à 3,793 V, piste 2 de 0,186 à 1,888 V.
En cas de défaut de lecture (ADS muet ou tension aberrante), les sorties passent sur « pédale relâchée », donc le moteur revient au ralenti.
**Conseillé :** ajouter le relais de bypass ci-dessous sur GPIO10, puis passer `USE_RELAY` à 1 dans `Pedale.ino`.

### Variante avec relais de bypass (schéma de principe)

```
               Relais DPDT 5 V (signal, contacts or : Omron G6K-2, HFD2)
 Pédale piste 1 ──┬────────────── NC1 ┐
                  │                   ├── COM1 ──► ECU entrée piste 1
 MCP4728 VOUTA ─[100Ω]─────────── NO1 ┘
 Pédale piste 2 ──┬────────────── NC2 ┐
                  │                   ├── COM2 ──► ECU entrée piste 2
 MCP4728 VOUTB ─[100Ω]─────────── NO2 ┘
                  │
 Lecture : chaque piste ──[15 kΩ]──┬──[22 kΩ]── GND      (piste1→GPIO1, piste2→GPIO2)
                                   └── 100 nF ── GND
 5 V référence ECU (alim pédale) ──[15 kΩ]──┬──[22 kΩ]── GND   → GPIO4
 5 V référence ECU ──► MCP4728 VDD (le DAC suit la référence ECU : sortie ratiométrique)
 MCP4728 SDA → GPIO8, SCL → GPIO9 (pull-up 4,7 kΩ vers 3,3 V), LDAC → GND, adresse 0x60
 GPIO10 ──[1 kΩ]── base BC547 → bobine relais (diode 1N4148 en parallèle) → 5 V
 Masse pédale = masse ECU = masse ESP (même point)
```
* **Relais au repos = pédale d'origine en direct.** Si le boîtier est éteint, planté ou en défaut, ou s'il n'y a plus de 5 V, ta voiture fonctionne comme d'origine. Le relais ne colle qu'après 200 ms de fonctionnement correct, et le DAC recopie déjà la pédale avant qu'il colle, donc pas d'à-coup.
* Chien de garde 300 ms : si le programme se bloque, l'ESP redémarre, le relais retombe et la pédale d'origine reprend la main.
* Lien E85 perdu (plus de 0,5 s) : l'aide redescend en rampe et le boîtier recopie simplement la pédale.

### Calibration (obligatoire avant d'activer l'aide)
Contact mis, moteur arrêté. Dans l'appli, onglet **Pédale** :
1. Pied levé → **« 1. Pédale relâchée »**
2. Pédale au plancher → **« 2. Pédale à fond »**
3. Onglet Système → **Sauvegarder**.

### Réglage de l'aide au ralenti (régulateur PI)
* **Régime visé** : le ralenti voulu, par ex. 950 tr/min.
* **S'active sous** : l'aide ne s'enclenche que pied levé et sous ce régime (1300 par défaut). Au-dessus de ce seuil + 200 tr/min, elle se coupe pour ne pas gêner le frein moteur ni la coupure d'injection en décélération.
* **Ouverture maxi** : 8 % au début, sans dépasser 40 %.
* Gains : P = 2, I = 1 pour démarrer. Si le régime oscille, baisse I, puis P. S'il remonte trop lentement, augmente I.
* Dès que tu appuies plus que l'aide, c'est ta pédale qui commande (recopie exacte des tensions).

---

## 4. Flasher les boîtiers

Les binaires sont prêts dans `livrables/`. Avec esptool (`python -m pip install esptool`) :

```bash
python -m esptool --port COM6 --baud 921600 write-flash 0 "livrables/E85_Master/E85_Master.ino.merged.bin"
```
```bash
python -m esptool --port COMx --baud 921600 write-flash 0 "livrables/Pedale/Pedale.ino.merged.bin"
```
L'ancien firmware est sauvegardé dans `flash_full_16MB.bin` (il se restaure de la même façon).

Pour recompiler (Arduino IDE ou `tools/arduino-cli`), il faut le core **esp32 ≥ 3.0** et la carte **ESP32S3 Dev Module**. Pour le boîtier E85 : *Arduino Runs On = Core 1*, *Events Run On = Core 0*, *Flash 16MB*, *USB CDC On Boot = Disabled*. Après toute modification de `index.html` : `python tools/gen_web_ui.py`.

---

## 5. Appli Android

1. Copie `livrables/BoitierE85.apk` sur le téléphone, ouvre-la et autorise « installer des applis inconnues ».
2. Android 10 minimum.
3. Au lancement, l'appli demande à se connecter au Wi-Fi `BoitierE85` (une fenêtre système, à accepter). Elle se lie ensuite à ce réseau : pas de souci avec la 4G qui « vole » la connexion.
4. Onglets :
   * **Moteur** : régime, % d'essence en plus (curseur, −1/+1/+5), durées par cylindre, position pédale.
   * **Injection** : correction par cylindre, temps mort, sécurités, type d'injection, filtre et inversion des entrées.
   * **Pédale** : tensions en temps réel, calibration, aide au ralenti.
   * **Système** : sauvegarde, simulateur de banc, infos (cause du dernier redémarrage, utile pour le diagnostic).

Les réglages s'appliquent **instantanément**. La sauvegarde en mémoire se fait moteur arrêté.

---

## 6. Première mise en route (dans cet ordre)

1. **Sur l'établi, sans injecteurs**, avec une LED + 1 kΩ sur chaque drain vers +12 V : flashe le boîtier E85 et connecte l'appli.
2. Onglet Système → **Simulation ECU** à 900 tr/min : les 3 LED clignotent en séquence et l'appli affiche le régime et les durées. Fais varier le %.
3. Branche le boîtier pédale. Le badge « Pédale ✓ » apparaît et les tensions s'affichent.
4. Sur la voiture, **moteur arrêté** : vérifie les écrêteurs, puis calibre la pédale.
5. Démarre avec **0 %**. Vérifie le ralenti, puis monte le % par paliers de 5, en surveillant le moteur (et la sonde lambda si tu en as une).
6. Active l'aide au ralenti en dernier, avec l'ouverture maxi à 5–8 %.

---

## 7. Liste des pièces (par boîtier)

**Boîtier E85** : ESP32-S3 DevKit (16 Mo), 3× PC817, 3× 1N4148, 3× 1 kΩ ½ W, 3× 2,2 kΩ, 3× IRLB8721 (ou IRLZ44N), 3× 100 Ω, 3× 10 kΩ, 3× UF4007, 3× zener 12 V 1,3 W (1N4742A), buck 12→5 V, SS34, SMBJ24A, fusible 2 A, 470 µF 16 V, 100 nF.

**Boîtier pédale** : ESP32-S3 DevKit, module MCP4728, relais DPDT 5 V à contacts signal, BC547, 1N4148, 3× 15 kΩ, 3× 22 kΩ, 3× 100 nF, 2× 100 Ω, 2× 4,7 kΩ, 1 kΩ, même alimentation que le boîtier E85.

---

## 8. Arborescence

```
firmware/E85_Master/   E85_Master.ino, protocol.h, web_ui.h (généré), build_opt.h
firmware/Pedale/       Pedale.ino, protocol.h (identique)
android/               projet Android (Gradle), interface : app/src/main/assets/index.html
tools/                 gen_web_ui.py, mock_boitier.js (faux boîtier pour tester l'appli sur PC), arduino-cli
livrables/             BoitierE85.apk + binaires prêts à flasher
flash_full_16MB.bin    sauvegarde de ton ancien firmware
```
