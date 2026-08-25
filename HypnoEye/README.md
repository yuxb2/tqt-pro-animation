# HypnoEye — T-QT Pro

Œil op-art : grosse pupille noire, anneaux concentriques autour, bandes qui
épousent la paupière puis s'aplatissent vers les bords. La pupille regarde
lentement à droite et à gauche, et les bandes du dessus et du dessous glissent
vers l'œil pour y disparaître.

Croquis autonome : ouvre `HypnoEye.ino` dans l'IDE avec les réglages du README
principal (§1). Rien à installer, il n'utilise que TFT_eSPI.

---

## 1. Boutons

| Bouton | Action |
| --- | --- |
| **IO00** (BOOT) | épaisseur des bandes : moyenne → fine → large |
| **IO47** (KEY) | fige ou relance le mouvement vers l'œil (le regard, lui, continue) |

Le bouton droit est le plus utile pour juger la forme : une fois le flux figé,
on voit exactement où les bandes rejoignent la paupière.

---

## 2. Comment c'est dessiné

Aucune courbe n'est tracée. Chaque pixel calcule une **phase**, et la bande est
blanche ou noire selon la partie fractionnaire de cette phase. Tout le dessin
tient donc dans la façon de construire la phase :

- **dans l'œil** : phase = distance à la pupille. Des cercles, donc.
- **en dehors** : phase = distance à la paupière, décalée dans le temps. Des
  bandes parallèles au bord de l'œil, qui glissent vers lui.

C'est ce qui rend le regard presque gratuit. Décaler la pupille et pencher la
phase suffit : du côté visé elle avance plus vite, donc les anneaux s'y
resserrent, et de l'autre côté ils s'écartent. Aucune courbe à replacer.

**Le penchement est accroché au bord de la pupille** — nul là-bas, croissant
vers l'extérieur. C'est ce qui fait que rien ne bouge tant que la pupille ne
bouge pas : le resserrement est la conséquence du déplacement, pas un effet
qui vit sa vie. Sans cet ancrage, l'anneau collé à la pupille glisse autour
d'elle et on voit les cercles se déformer d'eux-mêmes.

**La paupière est tracée à part.** C'est le seul trait qui ne bouge jamais :
un trait blanc, encadré d'une épaule noire pour qu'il ne se fonde pas dans une
bande blanche voisine. Les bandes qui dérivent vers l'œil viennent y mourir.

Deux conséquences visibles :

**Les coins.** Là où la paupière rejoint l'horizontale, l'angle est réglé par
`CORNER_P`. À `1.0` la paupière arrive avec une pente non nulle et le coin est
pointu ; au-dessus de `1.0` la pente s'annule et le raccord s'arrondit.

**Le gris.** L'onde utilisée est triangulaire, pas carrée. Sa pente donne
l'anticrénelage gratuitement, et surtout : là où les bandes se serrent plus
que ce que l'écran peut tenir (les pointes de l'œil), le motif s'affaisse
doucement vers le gris au lieu de partir en moiré.

---

## 3. Les réglages

### La forme

| Constante | Défaut | Effet |
| --- | --- | --- |
| `W_LENS` | `60.0` | demi-largeur de l'œil. À 60 les coins touchent presque le bord de l'écran. Baisse-la pour dégager les bandes horizontales de chaque côté. |
| `H_LENS` | `30.0` | demi-hauteur au milieu. |
| `CORNER_P` | `1.6` | arrondi du coin. `1.0` = coin pointu, `2.2` = raccord très doux et pointes très étirées. |
| `R_PUPIL` | `17.0` | rayon de la pupille. Au-delà de 22 elle mange les anneaux du haut. |

### Les bandes

| Constante | Défaut | Effet |
| --- | --- | --- |
| `PITCH_MID` / `_FINE` / `_WIDE` | `9` / `6.5` / `12` | épaisseur d'une bande noire + une blanche, en pixels. Le bouton gauche passe de l'une à l'autre. |
| `DUTY` | `0.18` | équilibre noir / blanc. `0` = parts égales, positif = trait blanc plus épais. |
| `AA_GAIN` | `0.125` | netteté du bord. Plus bas = plus flou ; plus haut = plus tranchant, mais le moiré revient dans les pointes. |
| `LID_W` | `0.16` | demi-épaisseur du trait de paupière, en fraction de `PITCH`. |
| `LID_GAP` | `0.16` | épaule noire de chaque côté du trait. À `0` il se fond dans les bandes voisines et cesse de se lire. |

> Le calage qui fait buter le premier anneau contre la pupille sur toute sa
> largeur est déduit de `DUTY`, il n'y a rien à régler pour ça. Si tu changes
> `DUTY`, le premier anneau reste correct.

### Le mouvement

| Constante | Défaut | Effet |
| --- | --- | --- |
| `GAZE_AMP` | `5.0` | débattement de la pupille, en pixels. Volontairement petit : le mouvement doit se remarquer sans qu'on le regarde. |
| `GAZE_P1` / `GAZE_P2` | `11.0` / `7.3` | les deux périodes du va-et-vient, en secondes. **Garde un rapport non entier** entre les deux, sinon le regard boucle visiblement. |
| `K_MAX` | `0.22` | resserrement des anneaux du côté regardé. `0` = la pupille se déplace sans déformer les anneaux. Au-delà de `0.5` le motif se met à onduler tout seul et on perd le lien avec la pupille. |
| `FLOW` | `4.0` | vitesse des bandes extérieures vers l'œil, en px/s. Négatif = elles sortent de l'œil au lieu d'y rentrer. |

---

## 4. Notes techniques

**La paupière est tabulée.** Sa hauteur ne dépend que de `x` et ne bouge
jamais, donc elle et sa pente sont calculées une fois au démarrage, colonne par
colonne. Ça sort deux `powf` et une racine de la boucle des 16 384 pixels.

**Le coût réel.** Les pixels hors de l'œil ne font qu'une soustraction et un
`floorf`. Seuls ceux de l'œil — environ un cinquième de l'écran — paient les
deux racines et les deux divisions. C'est ce déséquilibre qui rend l'animation
tenable à pleine cadence.

**Pas de tableau d'état.** L'image ne dépend que du temps : rien à initialiser,
rien à faire vieillir. C'est aussi pour ça qu'elle ne coûte qu'un kilo-octet de
RAM dans `CyberCycle`, où elle est la vue 11.
