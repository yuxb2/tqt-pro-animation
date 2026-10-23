# EscherCube — T-QT Pro

Le cube impossible d'Escher, en métal bleu-violet. Il pivote en allers-retours
et flotte de haut en bas, comme un cristal de l'End dans Minecraft. Sous son
centre, la barre du fond passe devant la barre de devant.

Il est **aussi la vue 17 de `CyberCycle/`**, où il a pris la place de
`SphereColor` (vue 6) dans le tour. Ce dossier-ci est le croquis autonome,
celui où on le travaille : ouvre `EscherCube.ino` dans l'IDE avec les réglages
du README principal (§1). Il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. Comment c'est construit

Le cube est vraiment en 3D : douze barres de section carrée, une par arête.
Chaque pixel lance un rayon, en projection orthogonale, et garde la barre
qu'il touche en premier. Dans le repère du cube, toutes les barres sont des
boîtes alignées sur les axes : le test d'un rayon, c'est trois multiplications
par des inverses calculés une fois par image, et quelques comparaisons. Le
cadre de chaque barre à l'écran écarte d'avance la plupart des tests.

**L'illusion tient en une règle.** Deux barres qui ne partagent pas de coin
et se croisent à l'écran se croisent deux fois : une fois au-dessus du centre
(la verticale du fond derrière une arête du dessus) et une fois en dessous
(la verticale de devant devant une arête du dessous). Au-dessus du centre, la
plus proche passe devant, comme dans un vrai cube. En dessous, c'est
l'inverse. C'est le dessin d'Escher : le haut du cube est vu d'en haut, le bas
vu d'en bas. Les barres qui partagent un coin gardent leur vraie profondeur,
sinon les coins se déchirent.

**Le lissage.** On calcule une couleur par coin de pixel (129 × 129) et chaque
pixel fait la moyenne de ses quatre coins. Les bords sont doux pour 2 % de
calcul en plus.

## 2. Pourquoi il ne fait pas de tour complet

L'illusion se défait à deux endroits :

- **De face** (0°, 90°…) : plus aucune barre ne croise une autre, c'est un
  cube ordinaire.
- **Pile sur l'arête** (45°) : le coin du fond tombe exactement derrière la
  verticale de devant. La règle compte aussi les croisements qui tombent sur
  un coin, pour que ce coin passe devant, mais la vue reste moins nette.

Le cube va donc et vient entre ces deux vues : autour de 62,5°, sur 17° en
tout, donc de 54° à 71°. Il ralentit aux deux bouts, comme le crâne.

Le cube est décalé d'un demi-pixel vers la droite (`CU_CX`). Sans ça, vu sur
l'arête, le pli de la barre de devant tombait pile sur une colonne
d'échantillons et faisait des dents de scie.

## 3. Les réglages

Choisis dans l'aperçu le 23/10/2026.

| Constante | Défaut | Effet |
| --- | --- | --- |
| `CU_SIZE` | `33.5` | demi-côté du cube, en pixels. |
| `CU_BEAM` | `0.34` | épaisseur des barres, en demi-côtés (17 % du côté). |
| `CU_ELEV` | `25.5` | on regarde le cube d'en haut, de tant de degrés. Vers 35°, la vue isométrique, les deux croisements se confondent. |
| `CU_AIM` | `62.5` | l'angle autour duquel il pivote. 0 et 90 : de face ; 45 : sur l'arête. |
| `CU_SWEEP` | `17` | angle total de l'aller-retour. À garder assez petit pour ne passer ni de face ni sur l'arête. |
| `CU_SWING_MS` | `14500` | un aller-retour complet. |
| `CU_BOB` / `CU_BOB_MS` | `2.5` / `3200` | le cube monte et descend de ± 2,5 px, en 3,2 s. |
| `CU_IMPOSSIBLE` | `1` | à 0, toutes les barres gardent leur vraie profondeur : c'est le cube ordinaire. |
| `CU_GRAD` | `0.35` | la lumière glisse du haut-gauche vers le bas-droite. |
| `CU_FOG` | `0.43` | les barres du fond s'assombrissent. |
| `CU_BG` | `0.28` | luminosité du bas de l'écran. Le haut reste noir. |
| `CU_SHADOW` | `0.55` | l'ombre au sol, plus petite et plus pâle quand le cube monte. |
| `CU_HUE` | `253` | teinte du métal et du fond : 0 rouge, 60 jaune, 120 vert, 180 cyan, 240 bleu, 300 magenta. |
| `CU_SAT` | `0.53` | intensité de la couleur. À 0, le métal redevient argenté. |

Dans `CyberCycle/`, les constantes gardent le même nom : le préfixe `CU_`
n'y était pas encore pris.

## 4. Notes techniques

**La vitesse est estimée, pas mesurée.** Douze tests de boîte au plus par
coin de pixel, la plupart écartés par le cadre de chaque barre : de l'ordre
de **30 à 40 images/s**. Le croquis plafonne à 40. Seul un flash le
confirmera.

**La RAM.** Le tampon d'écran (32 ko), deux rangées de coins (1 ko) et une
table de sinus de 4 ko. Dans `CyberCycle/`, la table de sinus est celle des
yeux des Yeux.

**Pas d'état.** L'image ne dépend que de l'heure.
