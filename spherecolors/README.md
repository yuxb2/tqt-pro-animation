# SpiralSphere — T-QT Pro

Sphère de points en spirale unique partant du pôle nord, rotation libre, teinte arc-en-ciel qui balaie l'espace par vagues.

---

## 1. Installation

Rien de nouveau par rapport à `GenerativeEye` : le sketch n'utilise que **TFT_eSPI** (version patchée LilyGO, celle du dépôt `Xinyuan-LilyGO/T-QT`, pas celle du Library Manager). Pas de OneButton, pas d'ArduinoJson.

| Réglage IDE | Valeur |
|---|---|
| Board | ESP32S3 Dev Module |
| Core ESP32 | **2.0.x** (2.0.14 max — au-delà TFT_eSPI casse) |
| Flash Size | 4MB (N4R2) ou 8MB (N8) |
| PSRAM | OPI PSRAM pour N4R2 / Disabled pour N8 |
| USB CDC On Boot | Enabled |

Upload bloqué → maintenir BOOT en branchant l'USB.

Si l'image est floue ou décalée : c'est le problème « old panel / new panel » du T-QT, il faut le bon fichier d'init dans TFT_eSPI (même symptôme que sur GenerativeEye).

---

## 2. Boutons

| Bouton | Action |
|---|---|
| **IO00** (BOOT) | change la loi de couleur : `WAVE` → `LONGITUDE` → `SPIRAL` |
| **IO47** (KEY) | gèle / relance la rotation (la couleur continue de tourner) |

Le bouton gauche est là exprès pour que tu compares les trois hypothèses en direct sur l'écran, sans recompiler. **Mode 0 (WAVE) = celui qui correspond à ta description** ; les deux autres sont là au cas où tu reconnaîtrais mieux l'original.

---

## 3. Les trois lois de couleur

| Mode | Ce que ça fait | À quoi ça ressemble |
|---|---|---|
| `WAVE` (0) | La teinte dépend de la projection du point sur un **axe qui tourne dans l'espace écran**, indépendamment de la sphère. | Dégradé arc-en-ciel qui traverse le globe et glisse dessus sans être accroché aux points. Par moments quasi monochrome, par moments arc-en-ciel complet. |
| `LONGITUDE` (1) | Teinte = longitude du point sur la sphère. | Quartiers en éventail rayonnant depuis le pôle, solidaires de la rotation. |
| `SPIRAL` (2) | Teinte = position le long de la spirale (donc la latitude). | Bandes concentriques qui suivent les tours de la spirale. |

---

## 4. Réglages — bloc CONFIG en haut du `.ino`

### Géométrie

| Constante | Défaut | Effet |
|---|---|---|
| `SPIRAL_TURNS` | `36` | Nombre de tours du pôle nord au pôle sud. Mesuré sur ta vidéo. Plus haut = spirale plus serrée. |
| `NUM_POINTS` | `2592` | **Garde la relation `NUM_POINTS = 2 × SPIRAL_TURNS²`** : c'est elle qui donne le maillage carré qu'on voit à l'équateur. 36 tours → 2592. 32 tours → 2048. 40 tours → 3200. |
| `SPHERE_RADIUS` | `54.0` | Rayon en pixels. À 54, le globe fait 108 px de diamètre et laisse une marge noire, comme sur ta vidéo. Max utile : 62. |

> Si tu changes `SPIRAL_TURNS` sans respecter la relation, les points deviennent des tirets (trop serrés en longitude) ou la spirale devient une grille de pointillés espacés.

### Rotation

| Constante | Défaut | Effet |
|---|---|---|
| `ROT_SPEED_X` | `0.30` | rad/s autour de l'axe horizontal. |
| `ROT_SPEED_Y` | `0.47` | rad/s autour de l'axe vertical. **Garde un rapport irrationnel** entre les deux (0.30 / 0.47) : sinon le mouvement boucle visiblement au bout de quelques secondes. |

Pour une rotation plus lente et hypnotique : `0.18` / `0.29`. Pour un tumbling nerveux : `0.55` / `0.83`.

### Couleur — c'est ici que tu vas jouer

| Constante | Défaut | Effet |
|---|---|---|
| `HUE_DRIFT` | `0.05` | Dérive globale, en tours de roue chromatique par seconde. 0.05 = un cycle complet toutes les 20 s. Monte à `0.12` si tu veux que ça défile plus vite. |
| `HUE_SPAN_BASE` | `0.50` | Étalement moyen de teinte sur la sphère. `0` = monochrome permanent, `1.0` = roue complète en permanence. |
| `HUE_SPAN_AMP` | `0.45` | **Le « flash » que tu décris.** Amplitude de la respiration autour de `HUE_SPAN_BASE`. Ici : oscille entre 0.05 (quasi monochrome) et 0.95 (arc-en-ciel plein). Mets `0` pour figer l'étalement. |
| `HUE_SPAN_PERIOD` | `9.0` | Période de cette respiration, en secondes. Sur ta vidéo j'ai relevé des passages arc-en-ciel vers t ≈ 0,5 / 2,3 / 6,0 / 10,5 s — soit une pulsation de l'ordre de 4 à 9 s. À affiner à l'œil. |
| `WAVE_SPIN` | `0.90` | Vitesse (rad/s) de rotation de l'axe de la vague. C'est ce qui rend l'effet **décorrélé** de la sphère : baisse à `0.3` et la vague semble suivre le globe, monte à `1.5` et elle le traverse en permanence. |
| `SATURATION` | `1.00` | `1.0` = couleurs pures. Descends vers `0.75` si le rendu réel te paraît plus lavé que le tien. |
| `SHADE_MIN` | `0.72` | Luminosité des points au bord du globe. `1.0` = rendu totalement plat (pas d'effet de volume), `0.5` = volume marqué. |
| `PALETTE_COSINE` | `0` | Mets `1` pour passer sur une palette cosinus au lieu du HSV. Ça produit naturellement des **bandes pâles / pastel** entre les couleurs vives — j'ai vu ce genre de bande délavée sur une de tes frames (t ≈ 5 s), donc essaie les deux. |

### Rendu

| Constante | Défaut | Effet |
|---|---|---|
| `TARGET_FPS` | `40` | Plafond de cadence. |
| `HUE_LUT_SIZE` | `96` | Finesse du dégradé. Sous 48 on voit des marches ; au-dessus de 128 c'est du gâchis de CPU. |
| `SHADE_LEVELS` | `4` | Niveaux d'ombrage en profondeur. `1` = pas d'ombrage du tout. |

---

## 5. Méthode d'ajustement (2 minutes montre en main)

1. **Flash tel quel**, regarde 30 s. La géométrie devrait déjà être identique — c'est la partie que j'ai mesurée précisément sur ta vidéo (espacement des points, nombre de tours, densité du vortex au pôle).
2. **Densité** : si le globe te paraît plus clairsemé que l'original, monte `SPIRAL_TURNS` à `40` **et** `NUM_POINTS` à `3200`. S'il paraît trop dense, descends à `32` / `2048`.
3. **Rythme du flash** : c'est le réglage le plus subjectif. Joue sur `HUE_SPAN_PERIOD` seul, entre `4.0` et `12.0`, jusqu'à retrouver la respiration de la vidéo.
4. **Caractère de la vague** : si elle te semble trop « collée » au globe, monte `WAVE_SPIN`. Si elle traverse trop vite, descends-le.
5. **Rendu des couleurs** : bascule `PALETTE_COSINE` à `1` une fois pour voir. Si les bandes pâles te rappellent l'original, garde-le.

---

## 6. Notes techniques

**Pourquoi le pas constant en latitude.** La spirale utilise `theta = π·i/N` (pas constant) et non une répartition équi-aire. Conséquence : les points se resserrent fortement près des pôles et fusionnent en un **trait continu** — c'est exactement le vortex qu'on voit au centre du pôle sur ta vidéo. Une répartition équi-aire (Fibonacci / Saff-Kuijlaars) donnerait des points bien séparés jusqu'au pôle, et le vortex disparaîtrait.

**Mesures relevées sur la vidéo.** Espacement des points ≈ 10,5 px vidéo pour un rayon de 122 px, soit un rapport de 0,086 → `π/0,086 ≈ 36` tours, et `N = 2×36² = 2592` pour que le maillage soit carré à l'équateur. Le rayon écran correspondant est ≈ 54 px.

**Mémoire.** Trois tableaux `float` de 2592 (≈ 31 ko) + deux tableaux d'index (≈ 5 ko) + le sprite 128×128 en 16 bits (32 ko) ≈ 68 ko. Large sur ESP32-S3. Si tu montes très haut en `NUM_POINTS`, passe les coordonnées en `int16_t` (facteur 2 gagné).

**Face arrière.** Les points de l'hémisphère arrière sont éliminés par `if (z2 <= 0) continue;`. Ça donne un globe opaque et divise le travail par deux — pas besoin de tri en profondeur.

**Pas de `delay()` long, pas de `WiFi`.** Le sketch tourne en boucle non bloquante sur `micros()`, donc tu peux greffer une récupération de données réseau dessus plus tard sans casser la fluidité.
