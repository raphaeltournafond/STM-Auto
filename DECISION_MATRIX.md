# STM-Auto — Matrice de décision

Surveillance d'huile moteur SWEE.BRZ (STM32F103C8 / Blue Pill). Comportement décisionnel :
phases de vie, situations, seuils, sorties sur 4 canaux. Brochage : `src/pins.h` (source de vérité).

> **WIP.** Sans le régime moteur (RPM via CAN), les planchers de pression sont prudents (type
> ralenti) ; le RPM permettra de les relever à mi/haut régime.

## 1. Architecture

- **Phases de vie** (séquentielles, une fois au démarrage) : `BOOT → INIT → RUN`, avec
  `INIT_FAIL` bloquant possible depuis `INIT`.
- **Situations** (évaluées en continu en `RUN`) : priorité 1→9, **on s'arrête à la première
  vraie** → une seule active à la fois.

4 canaux parallèles. **Secours :** LED et buzzer ne dépendent ni de l'écran ni de la carte SD.

| Canal | Rôle | Matériel |
|---|---|---|
| Volet | Régulation thermique / sécurité | 2 servos en tandem (PA6, PA7) |
| Écran | Valeurs + alertes détaillées | OLED SSD1306 I2C |
| LED | off / jaune / rouge ; intégrée en secours | WS2812B (PB15) + LED intégrée (PC13) |
| Son | Bips + voix | Buzzer (PB6) + module MP3 (USART1) |

## 2. Phases de vie

- **BOOT** — LED clignote vite, bip de mise sous tension, logo écran.
- **INIT** — auto-tests **tous bloquants** (échec → `INIT_FAIL`), affichés à l'écran, dans l'ordre :
  1. OLED (en premier : sans écran, repli LED + voix) · 2. carte SD du MP3 ·
  3. capteur température plausible · 4. capteur pression plausible · 5. cycle volet fermé→ouvert→fermé.
  LED clignote lentement.
- **INIT_FAIL** — **bloquant total**. LED : code dédié ; son : bip long ×N + voix défaut ;
  écran : code d'erreur + valeur brute (Ω / V).
- **RUN** — nominal, régi par §3. **Watchdog** matériel si la boucle se fige.

## 3. Matrice de situations (RUN) — priorité décroissante, première vraie gagne

| # | Situation | Température | Pression | Volet | Écran | LED | Son |
|---|---|---|---|---|---|---|---|
| 1 | Err. capteur temp | R > 1500 Ω (brut) | — | Ouvert séc. | `TEMP SENS ERR` + Ω brut | Rouge | Bips longs ×N + voix |
| 2 | Err. capteur press | — | V < 0,3 V (brut) | Ouvert séc. | `PRESS SENS ERR` + V brut | Rouge | Bips longs ×N + voix |
| 3 | Stop engine | > 122 °C | < 1,2 bar | Ouvert 90° | `STOP ENGINE!` | Rouge | Buzzer continu + voix |
| 4 | Chute pression | toute | < plancher §4 | Ouvert séc. | `LOW PRESS!` + bar | Rouge | Buzzer continu + voix |
| 5 | Surpression | toute | > 6,5 bar | inchangé | `OVER PRESS` + bar | Rouge | Buzzer continu |
| 6 | Surchauffe légère | 110–122 °C | OK | Ouvert 90° | Temp. clignotante | Jaune | Buzzer intermittent + voix |
| 7 | Régulation | 90–110 °C | OK | Ouvert 90° | Normal + `FLAP: OPEN` | Off | — |
| 8 | Normal | 70–89 °C | OK | Fermé 0° | Normal | Off | — |
| 9 | Moteur froid | < 70 °C | OK | Fermé 0° | Normal | Off | — |

- Erreurs capteur en tête : mesure non fiable → rien n'est jugeable ; testées sur la grandeur
  **brute** (Ω, V) avant conversion.
- Volet **ouvert par défaut** en cas de doute/panne (privilégie le refroidissement).
- Surpression : ouvrir le volet ne réduit pas la pression → alerte seule, volet inchangé.

## 4. Plancher de pression adaptatif (situation 4)

Seuil « pression basse » fonction de la bande de température (évite les faux positifs en conduite
sportive). Valeurs type ralenti.

| Bande température | Plancher OK | Critique sous | Réarmement |
|---|---|---|---|
| < 70 °C | ≥ 2,0 bar | 1,5 bar | 1,8 bar |
| 70–89 °C | ≥ 1,2 bar | 0,9 bar | 1,1 bar |
| 90–110 °C | ≥ 1,0 bar | 0,8 bar | 1,0 bar |
| 110–122 °C | ≥ 0,8 bar | 0,6 bar | 0,8 bar |
| > 122 °C | suspecte | combiné temp : < 1,2 bar | — |

> RPM (CAN) à venir → relever ces planchers à mi (~3000) / haut (6000+) régime.

## 5. Hystérésis

- **Volet (température)** : ouvre à **92 °C**, ferme à **85 °C**.
- **Pression** : réarmement via la colonne dédiée §4 (repasser au-dessus du réarmement, pas
  seulement du critique).

## 6. Acquittement

- **Bouton tactile** : acquitte l'alarme courante → coupe son + voix, **garde** écran + LED tant que
  la condition persiste.
- Une situation **nouvelle de priorité supérieure** repart en son malgré un ack précédent.
- **Retour auto** au nominal dès que la condition disparaît.
- **Multi-alarmes** : l'écran **alterne** entre les affichages.

## 7. Messages vocaux (MP3, carte SD)

Un `.mp3` numéroté par message. Langue : *à confirmer (français présumé)*.

| Fichier | Déclencheur | Contenu |
|---|---|---|
| `0001` | Fin INIT OK | Jingle « système prêt » |
| `0002` | Situation 6 | « Température élevée » |
| `0003` | Situation 4 | « Pression d'huile basse » |
| `0004` | Situation 3 | « Surchauffe, coupez le moteur » |
| `0005` | Situations 1, 2, INIT_FAIL | « Défaut capteur » |

## 8. Brochage (carte terminée — détails dans `src/pins.h`)

| Fonction | Broche | Fonction | Broche |
|---|---|---|---|
| Capteur température | PA1 | Buzzer (tone→TIM3) | PB6 |
| Capteur pression | PA0 | Bouton tactile | PB5 |
| Servos volet ×2 (Servo→TIM2) | PA6 / PA7 | LED RGB WS2812B | PB15 |
| OLED I2C2 | PB10 / PB11 | LED secours intégrée | PC13 |
| Module MP3 (USART1) | PA9 / PA10 | CAN (SN65HVD230) | PA11 / PA12 |
| Debug Serial (USART2) | PA2 / PA3 | | |

Alim : 12 V→5 V externe → servos + capteur pression + STM32 (broche 5 V) ; 3,3 V carte → OLED + CAN ;
masses communes ; 470 µF par servo. CAN = broches USB → retirer le pull-up USB sur PA12.
USART1 = MP3, USART2 = console de débogage (`Serial` remappé via `build_flags`).

## 9. Points ouverts

- RPM via CAN (améliore la logique pression).
- Seuil de surpression (6,5 bar) à confirmer ou situation 5 à abandonner.
- Liste / langue des messages vocaux.
- Autres valeurs CAN à afficher (temp. eau, vitesse…).
