# PaperPond — T-QT Pro

Une mare en papier découpé : des feuilles empilées, du bleu pâle au bord
jusqu'au bleu nuit au fond, dont les bords ondulent doucement. Deux poissons
rouges en papier plié nagent au-dessus, et leur ombre glisse d'une feuille à
l'autre.

Elle est **aussi la vue 18 de `CyberCycle/`**, où elle a pris la place de
`Morph` (vue 0) dans le tour, entre l'œil pochoir et l'œil aux rideaux. Ce
dossier-ci est le croquis autonome, celui où on la travaille : ouvre
`PaperPond.ino` dans l'IDE avec les réglages du README principal (§1). Il
n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. Comment c'est construit

**Les feuilles.** Toutes les découpes sont les lignes de niveau d'un seul
champ : une ellipse, plus quatre ondes qui dérivent lentement. Deux lignes de
niveau ne se croisent jamais, donc les feuilles restent emboîtées quoi qu'il
arrive, et chacune ondule un peu autrement que sa voisine. Avec trop
d'ondulation, des îlots de papier se détachent.

**Les ombres.** Le champ divisé par sa pente donne, en chaque point, la
distance au bord de la feuille du dessus : c'est l'ombre de contact. La même
distance lue quelques pixels vers la lumière (en haut à gauche) donne l'ombre
portée. Le bord des feuilles tourné vers la lumière reçoit un liseré clair,
et un grain fixe imite la fibre du papier.

**Les poissons.** Chacun suit une courbe fermée : un cercle, plus une boucle
qui tourne deux fois plus vite en sens inverse. Tant que `FI_WANDER` reste
sous 0,5, la vitesse ne tombe jamais à zéro : ils ralentissent et repartent,
sans s'arrêter. Le second tourne dans l'autre sens, 30 % plus lentement, et
passe sous le premier quand ils se croisent. Le corps se plie selon la
courbure du trajet, et la queue bat plus fort quand ils accélèrent. Le dos
est un pli : une moitié éclairée, l'autre à l'ombre. Leur ombre est plus
décalée sur les feuilles profondes, plus loin d'eux.

**Le lissage.** Une couleur par coin de pixel (129 × 129), et chaque pixel
fait la moyenne de ses quatre coins, comme `EscherCube`.

## 2. Les réglages

Choisis dans l'aperçu le 25/10/2026.

| Constante | Défaut | Effet |
| --- | --- | --- |
| `PD_LAYERS` | `6` | feuilles de papier, la plus pâle comprise (3 à 12). |
| `PD_RX` / `PD_RY` | `69` / `73` | demi-largeur et demi-hauteur de la mare, en pixels. |
| `PD_EDGE` | `0.00` | place de la première découpe : 0, c'est sur l'ellipse. |
| `PD_CORE` | `0.75` | place de la dernière découpe : 1, c'est au centre. |
| `PD_WOBBLE` | `0.120` | ondulation des bords. |
| `PD_DRIFT_MS` | `10000` | un cycle complet de la dérive. |
| `PD_CURVE` | `0.60` | répartition des bleus : sous 1, on fonce vite dès les premières feuilles. |
| `PD_SHADOW` | `0.62` | force des ombres entre les feuilles. |
| `PD_SHADOW_LEN` | `5` | décalage de l'ombre portée, en pixels entiers. |
| `PD_SOFT` | `3.0` | flou de l'ombre, en pixels. |
| `PD_RIM` | `0.10` | liseré clair sur la tranche éclairée. |
| `PD_GRAIN` | `0.040` | grain du papier. |
| `FI_COUNT` | `2` | 1 ou 2 poissons. |
| `FI_LEN` | `37` | longueur d'un poisson, en pixels. |
| `FI_WIDTH` | `0.170` | demi-largeur du corps, en longueurs. |
| `FI_HUE` | `6` | teinte : 0 rouge, 30 orange, 45 doré. |
| `FI_RANGE` | `0.50` | jusqu'où ils s'éloignent du centre, en fraction de la mare. |
| `FI_WANDER` | `0.30` | 0 : un cercle ; 0,45 : des boucles serrées. |
| `FI_SWIM_MS` | `26000` | un tour complet (le second poisson met 30 % de plus). |
| `FI_WAG` / `FI_WAG_MS` | `0.130` / `800` | battement de queue, en longueurs, et sa durée. |
| `FI_LIFT` | `6.5` | hauteur au-dessus de l'eau : décalage de leur ombre, en pixels. |
| `FI_SHADOW` | `0.45` | force de l'ombre des poissons. |
| `FI_FOLD` | `0.16` | contraste du pli du dos. |

Dans `CyberCycle/`, les constantes gardent le même nom : les préfixes `PD_`
et `FI_` n'y étaient pas pris.

## 3. Notes techniques

**La vitesse est estimée, pas mesurée.** 21 000 évaluations du champ par
image, puis quelques lectures par coin de pixel ; les poissons ne coûtent
que dans leur cadre. De l'ordre de **30 à 40 images/s**. Le croquis plafonne
à 40. Seul un flash le confirmera.

**La RAM.** Ici, la grille entière du champ (149 × 149 flottants, 89 ko), le
tampon d'écran (32 ko) et deux rangées de coins. `CyberCycle/` ne peut pas
prendre 89 ko de plus sans rogner le tas dont la mise à jour OTA a besoin : il
ne garde qu'un anneau de 16 rangées de la grille (10 ko), logé dans
`viewScratch`, le tampon que partagent déjà `CelestialScan` et `WorldRing`.
Un coin ne lit que sa rangée, ses voisines et la rangée `PD_SHADOW_LEN` plus
haut, et une vérification à la compilation refuse une ombre trop longue pour
l'anneau. Le rendu est le même, et la vue 18 ne coûte que 264 octets de RAM
de plus que la v1.0.21. Les rangées de coins et la table de sinus
sont celles du cube d'Escher et des yeux des Yeux.

**Pas d'état.** L'image ne dépend que de l'heure.
