# WorldRing — T-QT Pro

Globe filaire blanc sur fond noir, continents pleins, anneau orbital, et une
phrase qui fait le tour de l'écran : « THE WORLD IS WATCHING », répétée deux
fois.

Croquis autonome : ouvre `WorldRing.ino` dans l'IDE avec les réglages du
README principal (§1). Rien à installer, il n'utilise que TFT_eSPI.

---

## 1. Changer le texte

Une seule ligne, en haut du `.ino` :

```c
#define RING_TEXT      "THE WORLD IS WATCHING\x07"
#define RING_REPEAT    2
```

`\x07` dessine le petit losange qui sert de séparateur entre deux passages.
Tu peux l'enlever, le remplacer par un espace, ou mettre autre chose.

**La taille des lettres s'ajuste toute seule.** Le sketch compte les
caractères, divise le cercle en autant de parts égales et calcule la taille
qui remplit la part. Une phrase plus longue donne des lettres plus petites,
une phrase plus courte des lettres plus grandes — dans les deux cas le tour
est exactement bouclé, sans trou ni chevauchement.

Quelques repères, avec `RING_REPEAT 2` :

| Longueur de la phrase | Rendu |
| --- | --- |
| 14–18 caractères | lettres à leur taille maximale (plafond `TEXT_SCALE_MAX`) |
| ~22 caractères | le réglage actuel, bien rempli |
| plus de 30 | ça devient serré, passe à `RING_REPEAT 1` |

La police couvre A–Z, 0–9 et la ponctuation courante. Les minuscules sont
tracées comme des capitales. Un caractère inconnu est simplement sauté — si
une lettre manque, ajoute-la dans `tools/vecfont.py` et relance le générateur
(§4).

---

## 2. Boutons

| Bouton | Action |
| --- | --- |
| **IO00** (BOOT) | style du globe : complet → filaire nu → continents seuls |
| **IO47** (KEY) | met l'anneau de texte en rotation, ou le refige |

Le style « filaire nu » est celui du logo de référence (pas de continents),
« continents seuls » enlève le quadrillage. Le bouton est là pour comparer
en direct sans recompiler ; une fois choisi, fixe le défaut en changeant
`globeStyle`.

---

## 3. Les réglages

### Le texte

| Constante | Défaut | Effet |
| --- | --- | --- |
| `TEXT_R` | `57.0` | rayon de la ligne médiane du texte. Au-delà de 58 les lettres touchent le bord. |
| `TEXT_PHASE_DEG` | `0.0` | où commence la phrase sur le cercle. `0` = premier caractère à midi. |
| `TEXT_SPIN_ON` | `0` | `1` pour que l'anneau tourne dès le démarrage. La référence est fixe. |
| `TEXT_SPIN_DPS` | `5.0` | vitesse quand il tourne, en °/s. 5 = un tour en 72 s. |
| `TEXT_BOLD` | `1` | trait doublé. À `0` c'est plus fin et plus fragile à cette taille. |
| `TEXT_FILL` | `0.86` | part de la place allouée à chaque lettre qui est réellement occupée. Baisse à `0.78` pour aérer, monte à `0.92` pour resserrer. |

### Le globe

| Constante | Défaut | Effet |
| --- | --- | --- |
| `GLOBE_R` | `38.0` | rayon en pixels. Au-dessus de 42 il rentre dans l'anneau orbital. |
| `TILT_DEG` | `16.0` | inclinaison. Plus haut = on voit davantage le pôle nord, et l'Arctique finit par former une calotte blanche uniforme. Plus bas = plus équatorial, continents mieux reconnaissables. |
| `SPIN_DPS` | `22.0` | °/s. Un tour en ~16 s. |
| `MERIDIANS` | `18` | méridiens (tous les 20°). À 24 le maillage devient très serré à 76 px de diamètre. |
| `PARALLEL_STEP` | `20` | espacement des parallèles, en degrés. |
| `COL_LINE_LEVEL` | `105` | gris du quadrillage. C'est lui qui empêche les continents blancs de se fondre dans les lignes — ne monte pas trop. |

### L'anneau orbital

Celui du logo, à ne pas confondre avec l'anneau de texte.

| Constante | Défaut | Effet |
| --- | --- | --- |
| `SHOW_ORBIT` | `1` | `0` pour l'enlever (rendu plus proche de la vidéo). |
| `ORBIT_R` | `49.0` | rayon. Coincé entre le globe (38) et le texte (~53 au bord intérieur). |
| `ORBIT_SQUASH` | `0.28` | `0` = vu par la tranche, `1` = cercle de face. |
| `ORBIT_PREC_DPS` | `6.0` | précession, °/s. `0` pour le figer comme sur le logo. |

---

## 4. Comment les continents sont faits

Le fichier `world_map.h` est une carte équirectangulaire 256 × 128 à un bit
par case (4 ko), générée depuis des contours grossiers écrits en dur dans
`tools/world.py`. À 76 px de diamètre les continents ne sont que des taches
de toute façon — c'est le rendu de la vidéo de référence.

Pour la modifier (redessiner un contour, ajouter une île) :

```bash
cd WorldRing/tools && python3 generate.py
```

Le script régénère `world_map.h` **et** `vecfont.h` (la police vectorielle,
définie dans `tools/vecfont.py`). Il n'a besoin que de Python 3, sans
dépendance. `python3 world.py` seul affiche la carte en ASCII dans le
terminal, ce qui suffit pour vérifier un contour avant de régénérer.

### Pourquoi ça tourne vite

L'inclinaison du globe ne change jamais et la rotation se fait autour de
l'axe des pôles. Pour un pixel d'écran donné, la latitude est donc **fixe**,
et la longitude n'est qu'un décalage entier. Toute la trigonométrie inverse
(`asin`, `atan2`) est faite une seule fois au démarrage dans une table ; la
boucle d'affichage ne fait plus qu'une soustraction sur un `uint8_t` — qui
reboucle toute seule modulo 256, d'où le choix de 256 colonnes pour la carte.

C'est aussi pour ça que le globe ne peut pas basculer librement dans cette
animation : ce serait une autre méthode de rendu.
