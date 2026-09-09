# SkullTurn — T-QT Pro

Un crâne en volume, éclairé, qui tourne lentement sur lui-même. Deux couleurs,
pas une de plus : les demi-teintes sont tramées.

Elle est **aussi la vue 13 de `CyberCycle/`**, et la première du tour : c'est
elle qui démarre quand la carte s'allume. Ce dossier-ci est le croquis
autonome, celui où on la travaille — ouvre `SkullTurn.ino` dans l'IDE avec les
réglages du README principal (§1). Rien à installer, il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. Boutons

| Bouton | Action |
| --- | --- |
| **IO00** (BOOT) | finesse : 1 rayon pour quatre ↔ 1 rayon par pixel |
| **IO47** (KEY) | mouvement : balancement ↔ tour complet |

Dans `CyberCycle/`, où le bouton gauche sert à changer d'animation, c'est le
bouton droit qui fait passer l'amplitude : **110° → 360° → 180°**.

---

## 2. Comment c'est construit

Il n'y a ni modèle 3D, ni maillage, ni texture, ni tampon de profondeur. Le
crâne est un assemblage de **dix-huit ellipsoïdes** : neuf qu'on ajoute pour la
matière, neuf qu'on retire pour les creux — orbites, ouverture nasale, fente de
la bouche, fosses temporales, dessous du menton. Tout tient dans `SK_PRIM`, six
nombres par ligne : le centre, puis les trois rayons. Pour changer la tête du
crâne, on déplace ces nombres, et rien d'autre.

**Chaque pixel envoie un rayon.** Un ellipsoïde traversé par un rayon, c'est une
équation du second degré, rien de plus. On les résout toutes les dix-huit d'un
coup, ce qui donne pour chacune un intervalle d'entrée et de sortie — et à
partir de là, le parcours ne fait plus que **comparer des nombres** : est-ce
que ce `t` tombe dans un creux ? est-ce qu'on est encore dans la matière ? Cette
séparation est ce qui rend la chose tenable sur la carte : le coût est dans les
dix-huit racines carrées, pas dans la logique.

**La vue est orthographique**, et c'est un choix de vitesse autant que de style.
La direction du rayon est alors la même pour tout l'écran, donc tout ce qui n'en
dépend que — la direction ramenée dans chaque sphère unité, le coefficient `a`
de chaque équation — se calcule **une fois par image** au lieu de seize mille
fois. C'est aussi pour ça qu'on fait tourner le rayon plutôt que le modèle :
faire tourner le crâne demanderait de bouger dix-huit primitives à chaque image
et de tout recalculer.

---

## 3. Ce qui fait l'image, plus que la géométrie

**`CAVITY`.** Une paroi creusée reçoit moins de lumière qu'une surface nue,
parce qu'elle ne voit qu'un bout de ciel. Une ligne :

```c
if (inward) v *= CAVITY;
```

Sans elle, les orbites se lisent comme des bols éclairés et l'ensemble comme une
boule. Avec, ce sont des trous, et le crâne apparaît. C'est le changement qui a
le plus compté, plus que n'importe quel déplacement d'ellipsoïde.

**La trame ordonnée.** Le seuil de noircissement change d'un pixel à l'autre
selon une petite table de 4×4. C'est ce qui fabrique des demi-teintes avec deux
couleurs, sans jamais afficher de gris — l'écran ne reçoit que du noir et du
blanc.

> **Le seuil est indexé sur le rayon, pas sur le pixel écran.** Ça a l'air d'un
> détail, c'en est un jusqu'au moment où on passe en demi-résolution : les
> rayons ne tombent alors que sur les coordonnées paires, donc on ne tirerait
> que quatre seuils sur seize — tous du côté sombre — et l'image partirait
> entièrement en blanc. C'était le cas dans la première version, et ça ne se
> voyait pas parce que le mode « Gris » de l'aperçu, lui, ne trame pas. La
> leçon : quand on juge un rendu 1 bit, il faut le juger en 1 bit.

**Le liseré de tranche** (`RIM`). Le fond est noir et l'os aussi, là où il
s'éloigne de la lumière. Sans un peu de clair sur la tranche, la silhouette se
dissout dans le fond.

---

## 4. L'arrière

La première version avait un arrière raté : une boule lisse posée sur un bloc
rectangulaire, avec des bouts d'os qui pendaient plus bas que le crâne. Il a été
refait, et les deux corrections **n'ajoutent aucune pièce**.

**La boîte crânienne descend.** Sur un vrai crâne, la partie qui contient le
cerveau ne s'arrête pas à mi-hauteur : elle descend derrière, jusqu'au niveau
des dents. On allonge donc l'ellipsoïde vers le bas *en laissant le sommet du
dôme où il est* — un rayon qui passe de `0.53` à `0.68`, et un centre qui
descend d'autant. Le dôme ne bouge pas d'un pixel, mais l'arrière tombe de
`y = -0.27` à `y = -0.58`.

**L'arche de la mâchoire est évidée par l'arrière.** Un ellipsoïde creusé dans
l'intérieur de la mandibule, qui s'arrête avant le menton (`z = 0.40`, alors que
la face avant du menton est en `z = 0.54`). Ce creux fait deux choses d'un
coup :

- il ouvre le dessous du crâne **en U**, ce qui donne enfin une structure à voir
  quand le crâne passe de dos ;
- il supprime la masse qui pendait sous l'occiput — c'était la mandibule, et
  elle descendait onze pixels sous le crâne.

Le menton, lui, n'est pas touché : la coupe s'arrête avant lui, et de face il
reste exactement ce qu'il était.

**C'était la contrainte.** L'avant était bon et ne devait pas bouger. Chaque
essai a donc été vérifié en comptant les pixels qui changent de face, en tramé,
aux angles utiles : **0,6 % à 0°, 1,4 % à −30°**, et ces pixels-là sont ceux du
bord arrière du dôme, qu'on voulait justement voir descendre. C'est aussi comme
ça qu'on a trouvé le coupable : une sonde qui dit, pour chaque pixel allumé,
quel ellipsoïde le tient — la mandibule, primitive 4.

Et ça ne coûte rien : dix-huit primitives au lieu de dix-sept, mais un creux
plus grand fait que moins de rayons vont chercher loin. Mesuré : 1,52 ms par
image contre 1,56.

---

## 5. Ce qui ne va pas encore

**Le profil.** Meilleur depuis la refonte de l'arrière, mais toujours le côté le
moins sûr : deux ellipsoïdes qui se croisent font une **arête vive**, jamais un
raccord doux, et de trois quarts arrière ça se voit encore un peu. Un raccord
doux demanderait un champ de distance et une marche de rayon, dix fois trop cher
pour la carte.

`SWEEP_DEG` est la réponse pratique : sous 360, le crâne **se balance** autour
de sa face au lieu de faire le tour, et ne montre jamais son mauvais profil. À
120° le mouvement reste vivant et tout ce qu'on voit se tient.

**La vitesse est une estimation.** 1,5 ms par image mesuré sur le Mac en pleine
finesse ; rapporté à un ESP32-S3, ça donne 10 à 18 images/s. C'est pour ça que
`HALF_RES` est à 1 par défaut, ici comme dans `CyberCycle` : un rayon pour
quatre pixels, donc quatre fois moins de travail, et le gros grain va plutôt
bien à l'écran. Ce n'est pas une mesure sur la carte, et seul un flash
tranchera.

---

## 6. Les réglages

| Constante | Défaut | Effet |
| --- | --- | --- |
| `SPIN_S` | `12.0` | durée d'un aller-retour du balancement. |
| `SWEEP_DEG` | `110` | amplitude. 360 = tour complet, qui traverse le profil ; 110 garde le crâne dans l'angle où il tient (§5). |
| `TILT_DEG` | `8.0` | inclinaison fixe. Positif = on le regarde d'un peu au-dessus. |
| `ZOOM_PX` | `64.0` | pixels d'écran par unité du modèle. Le crâne fait deux unités de haut, donc à 64 il remplit l'écran. |
| `LIGHT_AZ` / `LIGHT_EL` | `-11` / `37` | la lumière, en degrés. Elle est **fixe dans la pièce** : c'est le crâne qui tourne dedans, et c'est ce qui donne le volume. Presque de face et un peu au-dessus. |
| `AMBIENT` | `0.14` | ce qui reste de matière dans l'ombre. À zéro la face sombre disparaît dans le fond. |
| `RIM` | `0.14` | le liseré de tranche (§3). |
| `CAVITY` | `0.42` | la lumière restante au fond d'un creux (§3). À 1, plus de trous. |
| `GAMMA` | `1.05` | sous 1 ça éclaircit, au-dessus ça creuse. À 1 exactement, le `powf` sort du binaire tout seul — au-dessus il coûte un `powf` par pixel touché. |
| `TEETH` | `0` | les dents : des rayures dans l'ombrage, pas des volumes. Coupées : à cette taille et en demi-résolution elles brouillaient plus qu'elles ne disaient. |
| `BAYER_8` | `0` | 0 = trame 4×4, 1 = trame 8×8 (plus de nuances, grain plus fin). |
| `HALF_RES` | `1` | 1 = un rayon pour quatre pixels. |

---

## 7. Notes techniques

**Le coût réel.** Dix-huit équations du second degré par pixel, dont autant de
racines carrées, plus une poignée de comparaisons. La sphère englobante coupe
court sur les pixels de fond — un peu plus de la moitié de l'écran — avant même
la première équation.

**La RAM.** 33 ko : le tampon d'écran, et un demi-kilo de tables recalculées à
chaque image. La géométrie elle-même tient dans 432 octets.

**Pas d'état.** L'image ne dépend que du temps. Rien à initialiser, rien à faire
vieillir — comme les autres animations à phase du dépôt.
