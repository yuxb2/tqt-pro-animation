# BlueEye — T-QT Pro

L'œil bleu du thème **Couleurs** des Yeux, seul sur son écran. C'est l'icône
du gif, et il rejoue sa boucle, ralentie à 5 s : regard en haut à gauche puis
à droite, clignement, regard en bas, retour au centre, l'œil qui plisse des
deux paupières, saut en haut à gauche.

Elle est **aussi la vue 15 de `CyberCycle/`**. Ce dossier-ci est le croquis
autonome, celui où on la travaille : ouvre `BlueEye.ino` dans l'IDE avec les
réglages du README principal (§1). Il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. D'où elle vient

Des **Yeux** (`../Yeux`), l'œuvre murale à deux écrans : c'est l'œil de droite
du thème Couleurs, celui qui tournait déjà sur un T-QT. La boucle a été
relevée image par image sur la vidéo du gif. Le croquis garde les deux
retouches faites sur le mur :

- **Le bas reste une amande.** En regardant en bas, la paupière du bas ne
  descend que de 15 % (16 % ici, `BL_DOWN_BOT`), au lieu de 38 % au premier
  essai.
- **La forme suit la pupille sans attendre.** L'ouverture des paupières se
  calcule directement depuis la position de la pupille : dès qu'elle remonte,
  l'amande reprend sa forme.

Et une différence voulue : **ici l'œil plisse des deux paupières**. Sur le mur,
seule celle du haut se fermait ; la paupière du bas ne remontait que de 5 %,
ce qui ne se voyait pas. Elle remonte ici de 30 % (`BL_SQUINT_BOT`).

L'icône est aussi plus grande et la boucle plus lente, réglées dans l'aperçu
(§3).

## 2. Comment c'est construit

L'amande a des **bords paraboliques**, `|v| < H (1 − (u/W)²)`, avec une
demi-hauteur différente en haut et en bas : c'est ce qui la laisse s'arrondir
d'un côté quand l'œil regarde de l'autre. Toutes les tailles sont en pixels du
gif (500 × 500) ; `BL_SCALE` dit combien de pixels d'écran ça fait.

`blueGif()` donne, pour chaque instant de la boucle, la position de la pupille
et l'ouverture des deux paupières. Le clignement, le plissement et le regard en
bas ne sont que des multiplications de ces deux ouvertures. L'iris, la pupille
et le reflet suivent la pupille ; l'iris a le même bleu que le fond, comme sur
l'icône.

## 3. Les réglages

| Constante | Défaut | Effet |
| --- | --- | --- |
| `BL_SCALE` | `0.32` | pixels d'écran par pixel du gif. L'amande fait 126 px de large : elle remplit presque l'écran (101 px sur le mur). |
| `BL_W` / `BL_H` | `197` / `106` | demi-largeur et demi-hauteur de l'amande au repos, en px du gif (194 / 112 dans le gif). |
| `BL_IRIS_R` | `95` | rayon de l'iris bleu. |
| `BL_PUP_R` | `67` | rayon de la pupille. |
| `BL_LOOP_MS` | `5000` | une boucle. Le gif la joue en 4267 ; au-dessus, tous les gestes ralentissent ensemble. |
| `BL_DOWN_TOP` | `0.16` | en regardant en bas, la paupière du haut descend de 16 %. |
| `BL_DOWN_BOT` | `0.16` | et celle du bas s'ouvre de 16 %. Trop, et ce n'est plus une amande. |
| `BL_SQUINT` | `0.57` | quand l'œil plisse, la paupière du haut se ferme de 57 %. |
| `BL_SQUINT_BOT` | `0.30` | et celle du bas remonte de 30 %. |

Dans `CyberCycle/`, les mêmes constantes s'appellent `BE_…` (`BE_SCALE`,
`BE_LOOP_MS`…).

## 4. Notes techniques

**La vitesse a déjà été mesurée** : c'est le seul des trois yeux qui a tourné
sur cette carte. Sur le mur, le T-QT tenait 53 images/s pour ce même dessin, au
même nombre de pixels. Le croquis plafonne à 40.

**La boucle se répète à l'identique**, comme le gif : 180 tours sur un quart
d'heure de CyberCycle. Si ça fait trop, `BL_LOOP_MS` la ralentit
sans rien changer d'autre.

**Une remarque de compilation.** `blueGif()` rend ses quatre valeurs par
référence, et non dans une `struct`. L'IDE Arduino écrit les prototypes des
fonctions en tête du croquis, avant les `struct` qu'il déclare : une fonction
qui rend une `struct` ne compile pas.

**Pas d'état.** L'image ne dépend que de l'heure.
