# T-QT Pro animations

Animations pour LilyGo **T-QT Pro** (ESP32-S3, écran 128×128 GC9A01), avec mise
à jour OTA depuis GitHub.

Dépôt : https://github.com/yuxb2/tqt-pro-animation

Le croquis publié est `CyberCycle/` : treize animations qui tournent 15 minutes
chacune. Bouton gauche = animation suivante, bouton droit = variante.

> La treizième, `Coucou`, est **temporaire** : un bonjour multicolore qui
> tourne, posé en tête de liste pour voir une mise à jour OTA arriver. Elle
> s'enlève en retirant `12` de `viewOrder`.

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
  ignoré par Git). Sert aux flashs USB.
- **Sur GitHub** : Settings → Secrets and variables → Actions. Sert au firmware
  publié en OTA.

Les deux sont déjà en place pour la box actuelle.

### Plusieurs réseaux

Le T-QT accepte jusqu'à **quatre réseaux**. À chaque contrôle il scanne, garde
ceux qu'il reconnaît et se connecte au plus fort : maison, bureau, partage de
connexion du téléphone — il se met à jour là où il se trouve, sans rien avoir à
changer quand il déménage. S'il n'en reconnaît aucun, il coupe le Wi-Fi après le
scan (quelques secondes) et reprend l'animation.

**Sur GitHub**, ajoute des secrets par paires numérotées. La première est
obligatoire, les autres facultatives :

| Secret | |
| --- | --- |
| `OTA_WIFI_SSID` / `OTA_WIFI_PASSWORD` | obligatoire |
| `OTA_WIFI_SSID_2` / `OTA_WIFI_PASSWORD_2` | facultatif |
| `OTA_WIFI_SSID_3` / `OTA_WIFI_PASSWORD_3` | facultatif |
| `OTA_WIFI_SSID_4` / `OTA_WIFI_PASSWORD_4` | facultatif |

Puis publie une nouvelle version (§3) : c'est la compilation qui grave les
réseaux dans le firmware. Le log de l'Action affiche seulement `Wi-Fi networks
compiled into this firmware: N` — vérifie ce chiffre, rien d'autre n'est écrit
dans les logs.

**Sur le Mac**, `secrets.h` prend la même liste :

```c
#define OTA_WIFI_NETWORKS \
  { "Livebox-1234",   "motdepasse" }, \
  { "Bureau",         "autre-mot-de-passe" }, \
  { "iPhone de Yuri", "encore-un-autre" },
```

Attention à l'antislash en fin de ligne (sauf la dernière) et à la virgule
après chaque accolade. Un ancien `secrets.h` à un seul réseau, avec
`OTA_WIFI_SSID` et `OTA_WIFI_PASSWORD`, continue de marcher tel quel.

> Les deux listes sont indépendantes. Ajouter un réseau seulement dans
> `secrets.h` ne change rien à l'OTA : il faut le secret GitHub **et** une
> nouvelle version publiée.

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

Pour savoir quand les cartes ont réellement pris cette version, voir §9.

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

Trois d'entre eux ont leur propre README, et sont **aussi** repris dans
`CyberCycle/` (vues 7, 10 et 11) :

- `WorldRing/` — globe filaire avec continents et une phrase qui fait le tour
  de l'écran. Le texte se change sur une ligne, la taille des lettres s'ajuste
  toute seule.
- `CelestialScan/` — sphère en courbes de niveau, avec température et
  coordonnées, comme si on auscultait un objet céleste.
- `HypnoEye/` — œil op-art dont la pupille regarde à droite et à gauche en
  resserrant les anneaux du côté visé.

Les deux tables générées (`vecfont.h`, `world_map.h`) existent en double, dans
`WorldRing/` et dans `CyberCycle/` : l'IDE Arduino ne compile que les fichiers
posés dans le dossier du croquis, on ne peut pas les partager. C'est
`WorldRing/tools/generate.py` qui écrit les deux copies, donc lance-le plutôt
que d'éditer un `.h` à la main.

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

## 9. Savoir si les cartes ont pris la mise à jour

Après avoir publié, tu veux savoir si le message est arrivé. Le tableau est ici :

**https://github.com/yuxb2/tqt-pro-animation/blob/status/STATUS.md**

Une ligne par version, avec le nombre de cartes qui l'ont confirmée. Il est
rafraîchi toutes les demi-heures par le workflow `fleet-status.yml`, et tu peux
le forcer depuis l'onglet Actions. Ou en une commande :

```bash
gh api repos/yuxb2/tqt-pro-animation/releases/tags/v1.0.9 --jq '.assets[] | select(.name=="checkin.bin") | .download_count'
```

### Comment ça marche

Chaque Release embarque un fichier de trois octets, `checkin.bin`. Une carte
qui démarre sur une version qu'elle n'a pas encore signalée le télécharge une
fois, puis note la version en mémoire persistante et ne le redemande plus.
GitHub compte les téléchargements des fichiers de Release et publie ce compte
par son API : **ce compteur est le nombre de cartes qui tournent sur la
version**.

Il n'y a donc **aucun identifiant dans le firmware**, ce qui compte beaucoup :
le binaire est publié pour que n'importe qui puisse le télécharger, et tout
jeton qu'on y aurait mis serait distribué avec.

### Trois choses à savoir

**Le compteur retarde d'environ six minutes.** Mesuré. Une carte qui vient de
redémarrer ne s'affiche pas tout de suite ; ce n'est pas un échec.

**N'ouvre jamais `checkin.bin` dans un navigateur.** Ça compterait comme une
carte et fausserait le relevé. C'est le seul geste à éviter.

**Le check-in n'est pas sur le même chemin que l'OTA, exprès.** Le firmware
se télécharge depuis `raw.githubusercontent.com`, dont le certificat est
vérifié contre une racine épinglée. Le check-in, lui, part vers `github.com`,
qui utilise une autre autorité de certification : l'épingler aussi ferait
qu'une future rotation de certificat chez GitHub tuerait l'OTA sur toutes les
cartes, sans autre recours que le câble USB. Le check-in tourne donc **sans
vérification**, ce qui est sans conséquence — rien n'est envoyé, rien de la
réponse n'est lu, le seul enjeu est l'exactitude d'un compteur. Le chemin qui
installe du code, lui, garde sa vérification complète.

### Si le parc change de taille

Le tableau compare le compteur au nombre de cartes. Il est à 2 par défaut ;
pour le changer, crée une variable de dépôt `FLEET_SIZE` (Settings → Secrets
and variables → Actions → Variables).
