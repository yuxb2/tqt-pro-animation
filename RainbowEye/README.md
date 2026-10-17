# RainbowEye — T-QT Pro

L'œil arc-en-ciel du thème **Couleurs** des Yeux, seul sur l'écran du T-QT.
Seize amandes de couleur, chacune un peu plus tournée que la précédente,
pivotent autour du blanc de l'œil. Dans le blanc, la sphère rouge suit le
regard, et sa pupille glisse un peu dans le rouge.

Elle est **aussi la vue 14 de `CyberCycle/`**. Ce dossier-ci est le croquis
autonome, celui où on la travaille : ouvre `RainbowEye.ino` dans l'IDE avec
les réglages du README principal (§1). Il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. D'où elle vient

Des **Yeux** (`../Yeux`), l'œuvre murale à deux écrans : c'est l'œil de
gauche du thème Couleurs, celui du T-Display. Les seize couleurs relevées sur
l'affiche, la torsion, sa respiration et la boucle du regard sont reprises
telles quelles, et le calcul donne les mêmes pixels.

Le regard est celui de l'œil bleu (`BlueEye/`, vue 15) : la boucle relevée
image par image sur le gif. Sur le mur, les deux yeux regardaient ainsi
ensemble. Le gif la joue en 4,27 s ; ici elle est ralentie à 5,05 s.

**Une différence, voulue.** Dans le gif, le regard passe d'en haut à droite à
en bas d'un coup, à 1,08 s : c'est invisible, parce que l'œil bleu est fermé
par son clignement à ce moment-là. L'œil arc-en-ciel, lui, n'a pas de
paupière. La v1.0.16 reprenait la boucle telle quelle, et la sphère se
téléportait, ce qui se voyait bien sur la carte. Depuis la v1.0.17, elle glisse
vers le bas pendant le temps du clignement (0,95 → 1,22 s du gif) et arrive en
bas en même temps que le regard du gif. L'œil bleu garde le saut, caché comme
dans le gif.

## 2. Comment c'est construit

Chaque couche est une **amande à bords paraboliques**,
`|v| < H (1 − (u/W)²)`, tournée d'un angle à elle. Savoir si un pixel est
dedans ne demande aucune racine carrée.

Les couches sont **emboîtées** : on part de la couche du pixel précédent, qui
est presque toujours la bonne, et on descend ou on monte d'une couche. Cela
fait deux tests par pixel en moyenne au lieu de seize.

Le bord entre deux couches est lissé à partir de la distance du pixel au bord,
une distance approchée : l'écart à la parabole divisé par sa pente.

Le tourbillon tient en une ligne :

```c
a = CO_TWIST * k * (1 + CO_BREATH * sin(br − 0,06 k)) + CO_WAVE * sin(2 br − 0,1 k)
```

La couche `k` tourne de `k` fois la torsion. La torsion respire, avec un
léger retard d'une couche à l'autre, et une petite onde court du centre vers
l'extérieur.

## 3. Ce qui change par rapport au mur

- **Le cadrage.** Sur le T-Display, le blanc de l'œil occupait 130 px sur 170.
  Ici il en fait 74 sur 128 (`CO_SCALE = 0.125`), pour qu'on voie davantage de
  couches tourner autour.
- **Le tourbillon et le regard**, réglés dans l'aperçu : torsion plus forte
  (−10,4° par couche au lieu de −7°), respiration un peu plus rapide (8 s),
  pupille plus grande, sphère qui monte et descend davantage, boucle du regard
  plus lente.
- **L'horizontale.** Le T-Display était penché sur le mur ; ici l'œil est
  droit.
- **L'horloge.** Sur le mur, le temps venait de l'œil gauche par ESP-NOW. Ici
  c'est `millis()` (et `animTime` dans CyberCycle).

## 4. Les réglages

| Constante | Défaut | Effet |
| --- | --- | --- |
| `CO_SCALE` | `0.125` | pixels d'écran par pixel de l'affiche. Le blanc de l'œil fait 590 px de large sur l'affiche, donc 74 px ici. |
| `CO_TWIST` | `-0.029` | ce qu'une couche tourne de plus que la précédente, en tours (−10,4° ; −7° sur le mur). À zéro, les amandes sont toutes droites. |
| `CO_BREATH` | `0.30` | la torsion enfle et retombe de ± 30 %. |
| `CO_BREATH_MS` | `8000` | durée d'une respiration. |
| `CO_WAVE` | `0.006` | l'onde qui part du centre, en tours. |
| `CO_RED_R` | `133` | rayon de la sphère rouge, en px de l'affiche. |
| `CO_PUP_R` | `92` | rayon de la pupille (84 sur le mur). |
| `CO_LOOK_X` / `CO_LOOK_Y` | `109` / `31` | jusqu'où la sphère va de côté, et en haut ou en bas. |
| `CO_LOOP_MS` | `5050` | une boucle du regard. Le gif la joue en 4267. |

Dans `CyberCycle/`, les mêmes constantes s'appellent `RE_…` (`RE_SCALE`,
`RE_TWIST`…).

## 5. Notes techniques

**La vitesse est estimée, pas mesurée.** Sur le mur, le T-Display tenait
23 images/s pour 54 400 pixels sur ses deux cœurs. Le T-QT n'a que
16 384 pixels, sur un cœur : de l'ordre de **25 à 35 images/s**. Le croquis
plafonne à 40. Seul un flash le confirmera.

**La RAM.** Le tampon d'écran (32 ko), une table de sinus de 4 ko et dix-sept
couleurs.

**Pas d'état.** L'image ne dépend que de l'heure. Rien à initialiser hors de
la table de sinus et des couleurs, et rien à faire vieillir.
