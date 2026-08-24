# T-QT Pro animations

Animations pour LilyGo **T-QT Pro** (ESP32-S3, écran 128×128 GC9A01), avec mise
à jour OTA depuis GitHub.

Dépôt : https://github.com/yuxb2/tqt-pro-animation

Le croquis publié est `CyberCycle/` : dix animations qui tournent 15 minutes
chacune. Bouton gauche = animation suivante, bouton droit = variante.

---

## 1. Réglages Arduino IDE (à ne jamais changer)

Ces réglages doivent être **exactement** ceux-ci. Ce sont eux qui ont provoqué
l'écran noir la première fois : la carte n'a que 4 Mo de flash, et un firmware
compilé pour 16 Mo redémarre en boucle avant même d'allumer l'écran.

| Réglage | Valeur |
| --- | --- |
| Board | **ESP32S3 Dev Module** |
| Flash Size | **4MB (32Mb)** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |
| PSRAM | **QSPI PSRAM** |

Le reste peut rester par défaut. GitHub compile avec les mêmes réglages : ils
sont écrits dans `.github/workflows/release-ota.yml` (variable `FQBN`). Si tu
changes les réglages dans l'IDE, change-les aussi là-bas, sinon la version OTA
ne démarrera pas.

## 2. Wi-Fi

Le Wi-Fi n'est jamais dans le dépôt. Il vit à deux endroits :

- **Sur le Mac** : `CyberCycle/secrets.h` (créé depuis `secrets.example.h`,
  ignoré par Git).
- **Sur GitHub** : Settings → Secrets and variables → Actions, deux secrets
  nommés `OTA_WIFI_SSID` et `OTA_WIFI_PASSWORD`.

Les deux sont déjà en place. Si tu changes de box, mets à jour les deux, sinon
le T-QT ne pourra plus se mettre à jour.

## 3. Publier une nouvelle version

C'est le seul geste à retenir. Après avoir modifié une animation :

```bash
git add .
git commit -m "Nouvelle animation Sphere"
git push
git tag v1.0.2
git push origin v1.0.2
```

Le numéro de tag doit être **plus grand** que le précédent (`git tag` liste les
tags existants). GitHub compile, publie une Release, et met à jour la branche
`firmware`. Rien à modifier dans le code : le numéro de version est injecté
depuis le tag.

Vérifie que la compilation est verte dans l'onglet **Actions** avant d'éteindre
l'ordi. Si elle est rouge, l'ancienne version reste en place sur le T-QT : rien
n'est cassé.

## 4. Ce que fait le T-QT

- Au démarrage, il lance l'animation tout de suite.
- **15 secondes plus tard**, il se connecte au Wi-Fi et lit
  `firmware/ota-manifest.txt`. Si la version publiée est plus récente que la
  sienne, il la télécharge et redémarre dessus. Sinon il coupe le Wi-Fi.
- Ensuite il refait ce contrôle **toutes les 24 heures**.
- Pas de Wi-Fi, ou GitHub injoignable : il continue simplement avec la version
  déjà installée.

Le téléchargement est en HTTPS avec vérification du certificat (racine ISRG
Root X1, celle de Let's Encrypt utilisée par `raw.githubusercontent.com`).

Pour forcer une mise à jour sans attendre : débranche/rebranche le T-QT et
attends 20 secondes.

## 5. Modifier les animations

Tout est dans `CyberCycle/CyberCycle.ino` :

- **Ordre de passage** : le tableau `viewOrder` (vers la ligne 76). On peut y
  retirer, répéter ou réordonner les vues.
- **Durée** : `ANIM_MS`, 15 minutes par défaut.
- **Variante par défaut** de chaque vue : dans `setup()`.

Les autres dossiers (`RgbCube/`, `GlyphRain/`, …) sont des croquis séparés qui
ne partent pas en OTA. Seul `CyberCycle/` est publié.

## 6. Les bibliothèques dans `libraries/`

`libraries/TFT_eSPI` et `libraries/OneButton` sont des copies de celles du Mac,
volontairement versionnées dans le dépôt. Le TFT_eSPI officiel ne pilote pas le
même écran (séquence d'init GC9A01 différente) et donnait un écran noir en OTA.
GitHub compile avec ces copies, donc le firmware OTA est identique à celui de
l'IDE.

Si tu mets à jour TFT_eSPI dans l'IDE et que l'affichage change, recopie-la :

```bash
rsync -a --delete --exclude 'examples/' --exclude '.git*' --exclude '.github/' '/Users/yurimuin/Documents/Programs/Arduino/Arduino lib/libraries/TFT_eSPI/' libraries/TFT_eSPI/
```

## 7. Si l'écran est noir

Dans l'ordre :

1. **Rebrancher en USB et reflasher** depuis l'IDE avec les réglages du §1.
   C'est la seule vraie réparation, et elle marche toujours : l'OTA ne peut pas
   empêcher un flash USB.
2. Pour savoir ce qui se passe, ouvre le moniteur série (115200). Un message
   `Detected size(4096k) smaller than the size in the binary image header` =
   mauvais réglage Flash Size. `assert failed: do_core_init` = redémarrage en
   boucle, l'écran ne s'allume jamais.
3. Écran noir mais rétroéclairage allumé, sans boucle : c'est la config
   d'écran. Vérifie que `libraries/TFT_eSPI/User_Setup_Select.h` sélectionne
   bien `Setup211_LilyGo_T_QT_Pro_S3.h`.

## 8. Vérifier ce qui tourne réellement sur la carte

```bash
ls /dev/cu.usbmodem*
esptool.py --port /dev/cu.usbmodem1301 flash_id
```

`Detected flash size` doit afficher **4MB**. C'est ce chiffre qui commande le
réglage Flash Size du §1.

Attention : deux cartes ESP32-S3 sont branchées sur le Mac. Celle de 4 Mo est le
T-QT Pro ; l'autre en affiche 16 Mo et n'a rien à voir avec ce projet. Le numéro
de port change d'un branchement à l'autre, donc vérifie toujours la taille de
flash avant de téléverser.
