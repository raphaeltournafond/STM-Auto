# STM-Auto — Matrice de décision

Surveillance d'huile moteur SWEE.BRZ (STM32F103C8 / Blue Pill). Comportement décisionnel :
phases de vie, situations, seuils, sorties sur 4 canaux. Brochage : `src/pins.h` (source de vérité).

> **WIP.** Sans le régime moteur (RPM via CAN), les planchers de pression sont prudents (type
> ralenti) ; le RPM permettra de les relever à mi/haut régime.

## 1. Architecture

- **Phases de vie** (séquentielles, une fois au démarrage) : `BOOT → INIT → RUN`, avec
  `INIT_FAIL` possible depuis `INIT` (bloquant jusqu'à ACK ou 10 s).
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

- **BOOT** — LED clignote vite, bip de mise sous tension, logo écran. **Watchdog** matériel
  (IWDG, 2 s) armé dès le début du BOOT : un blocage pendant BOOT, INIT ou RUN (ex. bus I2C figé)
  provoque un reset.
- **INIT** — auto-tests (échec → `INIT_FAIL`), affichés à l'écran, dans l'ordre :
  1. OLED (en premier : sans écran, repli LED + voix) · 2. carte SD du MP3 (requête « nombre de
  fichiers » au DFPlayer : pas de réponse ou 0 fichier → échec) · 3. capteur température
  plausible · 4. capteur pression plausible · 5. cycle volet fermé→ouvert→fermé.
  LED clignote lentement.
- **INIT_FAIL** — bloquant jusqu'à **ACK** (contournement) ou **10 s**, puis `INIT` continue
  (étapes suivantes, puis `RUN`, qui signale lui-même un défaut capteur) : un reset en roulant
  (chute d'alim, connecteur) ne laisse jamais le moteur sans surveillance. LED + son : **code N** =
  numéro de l'étape en échec (1 OLED, 2 MP3, 3 temp., 4 press.) → N impulsions longues rouges +
  bips (0,6 s / 0,4 s), pause 1,6 s, en boucle ; voix défaut ; écran : étape en échec + valeur
  brute (Ω / V) + compte à rebours.
- **RUN** — nominal, régi par §3. Après un reset watchdog : redémarrage rapide directement en
  `RUN` (ni jingle, ni animation, ni cycle volet, ni `INIT_FAIL` — `RUN` signale lui-même un
  défaut capteur).

## 3. Matrice de situations (RUN) — priorité décroissante, première vraie gagne

| # | Situation | Température | Pression | Volet | Écran | LED | Son |
|---|---|---|---|---|---|---|---|
| 1 | Err. capteur temp | R < 10 Ω ou ≥ 25 kΩ (brut) | — | Ouvert séc. | `TEMP SENSOR ERROR` + `ERR` Ω brut | Rouge | Bips longs ×N + voix |
| 2 | Err. capteur press | — | V < 0,3 V ou > 4,8 V (brut) | Ouvert séc. | `PRESS SENSOR ERROR` + `ERR` V brut | Rouge | Bips longs ×N + voix |
| 3 | Stop engine | > 122 °C | < 1,2 bar | Ouvert 90° | `STOP ENGINE!` | Rouge | Buzzer continu + voix |
| 4 | Chute pression | toute | < plancher §4 | Ouvert séc. | `LOW PRESS!` + bar | Rouge | Buzzer continu + voix |
| 5 | Surpression | toute | > 9,5 bar | inchangé | `OVER PRESS` + bar | Rouge | Buzzer continu |
| 6 | Surchauffe légère | 110–122 °C | OK | Ouvert 90° | Temp. clignotante | Jaune | Buzzer intermittent + voix |
| 7 | Régulation | 90–110 °C | OK | Hystérésis §5 | Normal + `FLAP:` état réel | Off | — |
| 8 | Normal | 70–89 °C | OK | Hystérésis §5 | Normal + `FLAP:` état réel | Off | — |
| 9 | Moteur froid | < 70 °C | OK | Fermé 0° | Normal | Off | — |

- Erreurs capteur en tête : mesure non fiable → rien n'est jugeable ; testées sur la grandeur
  **brute** (Ω, V) avant conversion. La plage temp. valide est large pour le démarrage à froid
  (≈ 18 kΩ à −20 °C ; circuit ouvert ≈ 72 kΩ ; court-circuit ≈ 0 Ω) et identique en `INIT` et
  `RUN`. Sous 40 °C (fin de table, 925 Ω) la température affichée est plafonnée à 40 °C.
- Mesures : chaque cycle (~200 ms = 10 tranches de 20 ms) moyenne 40 échantillons ADC par capteur,
  4 par tranche, **répartis sur tout le cycle** → filtre l'ondulation allumage/alternateur et les
  pulsations de pression (une rafale unique de quelques µs ne le fait pas).
- Écran en défaut capteur : `ERR` + valeur brute (ex. `ERR 72.4k` circuit ouvert, `ERR 4.95V`
  court-circuit au 5 V ; `R` = ohm).
- Volet **ouvert par défaut** en cas de doute/panne (privilégie le refroidissement).
- Situations 7–8 : le volet suit l'hystérésis §5 (ouvre à 92 °C, ferme à 85 °C), pas la limite de
  bande à 90 °C. L'écran affiche donc `REGULATING` quand le volet est **ouvert** et `NORMAL` quand
  il est **fermé** (affichage seulement : LED, son et planchers de pression suivent la bande).
- Err. capteur press > 4,8 V : signal court-circuité au 5 V (ou masse capteur perdue) — l'ADC
  sature à 3,3 V soit 4,95 V côté capteur ; la table s'arrête à 3,01 V = 10 bar.
- Jauges écran : température 40–150 °C, pression 0–10 bar (le seuil de surpression reste visible
  sous la pleine échelle).
- Surpression : ouvrir le volet ne réduit pas la pression → alerte seule, volet inchangé.
- Situation 6 : la température clignote (≈ 0,5 s) tant que l'alarme surchauffe est active, même
  quand une autre alarme occupe la ligne d'état.
- Console de débogage (USART2) : une ligne CSV `T,t_ms,ohm,degC,volt,bar,situation,flap,ack`
  toutes les 500 ms (calibration au banc, enregistrement en roulage).

## 4. Plancher de pression adaptatif (situation 4)

Seuil « pression basse » fonction de la bande de température (évite les faux positifs en conduite
sportive). Valeurs type ralenti. Ce sont les **minimums génériques** (`PRESS_CRITICAL_MIN` /
`PRESS_REARM_MIN`) : une calibration propre au véhicule peut les **relever**, jamais les baisser.

| Bande température | Plancher OK | Critique sous (min.) | Réarmement (min.) |
|---|---|---|---|
| < 70 °C | ≥ 2,0 bar | 1,5 bar | 1,8 bar |
| 70–89 °C | ≥ 1,2 bar | 0,9 bar | 1,1 bar |
| 90–110 °C | ≥ 1,0 bar | 0,8 bar | 1,0 bar |
| 110–122 °C | ≥ 0,8 bar | 0,6 bar | 0,8 bar |
| > 122 °C | suspecte | combiné temp : < 1,2 bar | — |

**Calibration véhicule** (procédure : README « Calibration ») : enregistrer des trajets via la
console série (lignes `T,…`), puis `scripts/analyze_log.py` mesure par bande la pression basse
typique **P5** — à température donnée, la pression la plus basse est celle du ralenti — et propose :

- critique = max(minimum générique, **50 % × P5**) ; réarmement = max(minimum, critique + écart
  générique) ;
- bande > 122 °C (couplée à l'arrêt moteur, §3) jamais modifiée ;
- bande sans assez de données (< 2 min) : valeur actuelle conservée.

La référence est **figée** : pas d'apprentissage continu (il masquerait une usure lente). Refaire la
calibration volontairement, p. ex. après une vidange qui change de viscosité. Les valeurs retenues
sont relues puis compilées dans `lib/decision/decision.cpp` ; un `static_assert` refuse toute valeur
sous le minimum.

> RPM (CAN) à venir → même méthode avec une table température × régime (repère forum : ~10 psi
> soit ~0,7 bar par 1000 tr/min jugé « bon », 4–5 psi soit ~0,3 bar par 1000 tr/min « bas »).

## 5. Hystérésis

- **Volet (température)** : ouvre à **92 °C**, ferme à **85 °C**.
- **Pression** : réarmement via la colonne dédiée §4 (repasser au-dessus du réarmement, pas
  seulement du critique).
- **Surchauffe légère** (situation 6) : entre à **110 °C**, sort sous **108 °C**.
- **Surpression** (situation 5) : entre au-dessus de **9,5 bar**, sort à **≤ 9,2 bar**.
- **Confirmation** : une nouvelle situation doit être vue **3 cycles consécutifs** (~0,6 s) avant
  de devenir active → le bruit capteur ne fait pas clignoter les alertes (voix rejouée, LED,
  ack perdu).

## 6. Acquittement

- **Bouton tactile** : un appui pendant une alerte **non acquittée** acquitte l'alarme courante
  (dès l'appui, sans attendre le relâcher) → coupe son + voix, **garde** écran + LED tant que la
  condition persiste. Cet appui ne sert à rien d'autre (ni changement de vue, ni carrousel). Hors
  alerte ou alerte déjà acquittée, le bouton navigue entre les vues (§10). Tant que l'ack tient, la voix d'une nouvelle situation de priorité
  inférieure ou égale n'est pas jouée.
- Une situation **nouvelle de priorité supérieure** repart en son malgré un ack précédent.
- **Retour auto** au nominal dès que la condition disparaît.
- **Multi-alarmes** : l'écran **alterne** entre les alarmes actives, **2 s** chacune : l'alarme
  active (confirmée) d'abord, puis les autres par priorité. LED et son suivent l'alarme active
  seule. `STOP ENGINE` n'alterne pas (il combine déjà température et pression).

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

## 10. Écrans (vues) et carrousel

Un seul bouton : l'acquittement reste prioritaire (§6). Gestes **hors alerte en attente d'ACK** :

| Geste | Action |
|---|---|
| Appui court (relâché avant 1,5 s) | Vue suivante (toutes les vues, en boucle) |
| Appui long (≥ 1,5 s, déclenché sans relâcher) | Carrousel on/off — 2 bips courts = on, 1 bip long = off, message en tête d'écran 1,5 s |

| # | Vue | Contenu | Carrousel |
|---|---|---|---|
| 1 | Principale | Ligne d'état + jauges température / pression (écran d'alarme) | oui |
| 2 | Température | Valeur + courbe ~10 min (1 point / 5 s), max / min de la fenêtre | oui |
| 3 | Pression | Valeur + courbe ~10 min + plancher d'alarme courant §4 en pointillés | oui |
| 4 | Session | T max, P max, P min huile chaude (≥ 70 °C), temps ≥ 110 °C, durée | oui |
| 5 | Diagnostic | Ω / V bruts, T / P, situation, volet, ack, plancher, reset watchdog | non (banc) |

- **Carrousel** : vue suivante toutes les **5 s** (vue Diagnostic sautée). Un appui court
  avance d'une vue et relance le délai.
- **Alerte non acquittée** : la vue principale est **imposée** et le carrousel suspendu (le délai
  repart à la fin de l'alerte). Après ACK, la navigation reprend et l'alerte reste en ligne d'état,
  inversée, sur toutes les vues.
- Au démarrage : vue principale, carrousel off (rien n'est mémorisé entre deux mises sous
  tension). Statistiques et courbes repartent à zéro ; les mesures en défaut capteur sont ignorées
  (statistiques) ou laissent un trou (courbes).

## 9. Points ouverts

- RPM via CAN (améliore la logique pression).
- Seuil de surpression (9,5 bar) à confirmer ou situation 5 à abandonner.
- Liste / langue des messages vocaux.
- Autres valeurs CAN à afficher (temp. eau, vitesse…).
