# CelestialScan — T-QT Pro

Un corps céleste qu'on ausculte : sphère en pointillés parcourue de courbes de
niveau blanches, avec température et coordonnées dans les quatre coins.

Croquis autonome : ouvre `CelestialScan.ino` dans l'IDE avec les réglages du
README principal (§1). Rien à installer, il n'utilise que TFT_eSPI.

---

## 1. Boutons

| Bouton | Action |
| --- | --- |
| **IO00** (BOOT) | nouveau relevé — le champ est retiré au sort, l'objet change complètement |
| **IO47** (KEY) | densité des courbes : moyenne → fine → grossière |

Le bouton gauche est le plus utile pour juger : chaque tirage donne un
paysage différent (des boucles concentriques bien nettes, des grandes plages
lisses, ou un relief très découpé). Appuie une dizaine de fois pour voir la
famille complète avant de figer les réglages.

---

## 2. Ce qui bouge, et pourquoi

Les courbes ne sont pas dessinées à la main. Un champ scalaire vit sur la
sphère — la somme de quatre ondes concentriques autour d'axes qui précessent
lentement — et le sketch en extrait les isolignes par *marching squares* sur
un maillage 96 × 48. C'est la mécanique d'une carte isobarique, d'où les
boucles emboîtées, les creux en spirale, et les repères **H** / **L** posés
sur les extrema.

Les chiffres ne sont pas décoratifs non plus :

- les coordonnées en bas sont le point de la sphère **qui nous fait face**
  (la longitude défile avec la rotation, la latitude suit le balancement de
  l'axe) ;
- la température est la valeur du champ **à ce point-là**.

Donc quand une dépression passe au centre, la température plonge. Tout est
solidaire.

---

## 3. Les réglages

### La sphère

| Constante | Défaut | Effet |
| --- | --- | --- |
| `SPHERE_R` | `48.0` | rayon en pixels. Au-delà de 52 les relevés des coins passent sur le globe. |
| `SPIN_DPS` | `12.0` | °/s. Un tour en 30 s. |
| `TILT_DEG` | `14.0` | inclinaison moyenne de l'axe. |
| `TILT_WOBBLE` | `12.0` | amplitude du balancement. À `0` l'axe est figé et la latitude affichée ne bouge plus. |
| `TILT_PERIOD` | `47.0` | période du balancement, en secondes. Volontairement sans rapport simple avec la rotation, pour que le mouvement ne boucle pas visiblement. |

### Le champ

| Constante | Défaut | Effet |
| --- | --- | --- |
| `N_TERMS` | `4` | nombre d'ondes. À `2` c'est très lisse et lisible, à `6` ça devient un moiré. |
| `OMEGA_MIN` / `OMEGA_MAX` | `1.9` / `4.3` | plage de fréquences tirée au sort. Bas = grandes plages molles, haut = motif serré. |
| `FIELD_DRIFT` | `0.23` | vitesse de déformation du relief, en rad/s. À `0` le motif est figé sur la sphère et seule la rotation anime l'image. |

### Les courbes

| Constante | Défaut | Effet |
| --- | --- | --- |
| `DETAIL_START` | `0` | densité au démarrage : `0` moyen (14 niveaux), `1` fin (20), `2` grossier (8). Même choix que le bouton droit. |
| `N_LON` / `N_LAT` | `96` / `48` | finesse du maillage. En dessous de 72 × 36 les boucles serrées deviennent anguleuses. |
| `LIMB_Z` | `0.10` | marge laissée au bord du disque. Sans elle les courbes s'y empilent en un bourrelet blanc plein. Monte à `0.16` pour dégager davantage. |
| `SHOW_HL` | `1` | les lettres H / L. |
| `HL_MIN_GAP` | `16` | écart minimum entre deux lettres, en pixels, pour éviter deux H collés sur un même plateau. |

### Les relevés

| Constante | Défaut | Effet |
| --- | --- | --- |
| `SHOW_HUD` | `1` | tous les textes des coins. |
| `TEMP_BASE` | `-78.0` | température au repos, en °C. Les °F en découlent. |
| `TEMP_SPAN` | `9.0` | écart apporté par le champ. À `20` les relevés deviennent spectaculaires mais moins crédibles. |
| `GRAT_DOT_GAP` | `3` | espacement des pointillés du maillage. Plus haut = plus aéré. |

---

## 4. Notes techniques

**Le sinus tabulé.** Le champ réclame ~19 000 sinus par image. `sinf()` de la
libm coûterait plusieurs millisecondes ; une table de 1024 entrées avec
interpolation linéaire ramène ça sous la milliseconde. La phase est ramenée
dans `[0, 2π[` pour que la précision du `float` ne se dégrade pas après
plusieurs heures.

**Deux rangées suffisent.** Marching squares n'a jamais besoin que de la
rangée de latitude courante et de la suivante, donc les positions projetées
tiennent dans deux tampons de 97 points au lieu d'un tableau complet — 2 ko
plutôt que 57.

**Le bord.** Une cellule à cheval sur le limbe a des coins devant et derrière :
interpoler entre les deux donne un point qui traverse la sphère, et à l'écran
une longue corde parasite. On écarte donc les cellules dont un seul coin passe
sous `LIMB_Z`. C'est ce qui explique le liseré noir au bord du disque.

**Les tableaux parallèles.** `rowSX` / `rowSY` / `rowZ` seraient plus lisibles
en tableau de `struct`, mais le préprocesseur Arduino remonte les prototypes
de fonctions tout en haut du `.ino`, avant toute déclaration de type faite
dans le corps du fichier — un paramètre `struct Node *` ne compile pas.
