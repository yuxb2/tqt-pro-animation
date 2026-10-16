# CurtainEye — T-QT Pro

Le thème **Rideaux** des Yeux, ramené sur un seul écran comme sur l'affiche
d'origine : un œil fait de formes simples au milieu, et deux familles de
cercles noirs, centrées au-dessus et au-dessous de lui, qui grandissent sans
fin et se croisent en treillis. L'œil ne bouge pas : seul son reflet fait le
tour de la pupille.

Elle est **aussi la vue 16 de `CyberCycle/`**. Ce dossier-ci est le croquis
autonome, celui où on la travaille : ouvre `CurtainEye.ino` dans l'IDE avec
les réglages du README principal (§1). Il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript, avec un curseur par
constante : c'est là qu'on règle, dans le navigateur, sans reflasher.

---

## 1. D'où elle vient

Des **Yeux** (`../Yeux`), l'œuvre murale à deux écrans, dans la dernière
version de Rideaux :

- pas de clignement : il était raté, il a été retiré ;
- le reflet blanc fait le tour de la pupille en 7 s, comme si la lumière
  tournait ;
- les cercles ont été espacés deux fois et affinés d'un tiers.

## 2. Ce qui change par rapport au mur

- **Le centre des cercles.** Sur le mur, les deux familles étaient centrées
  entre les deux écrans : le T-QT ne voyait que des arcs venus de la gauche.
  Ici elles sont centrées sur l'œil, comme sur l'affiche.
- **Les réglages.** L'œil garde sa taille du mur, 115 px de large
  (`RI_SC = 1.15`). Réglés dans l'aperçu : des cercles plus serrés (un tous
  les 20 px), plus fins et plus rapides, des centres un peu plus proches de
  l'œil, et un reflet qui tourne en 5,6 s au lieu de 7.
- **Plus de table.** Sur le mur, l'œil était calculé une seule fois au
  démarrage, sur seize points par pixel, et rangé en PSRAM avec deux tables de
  distances. Ici tout est recalculé à chaque image, et chaque bord est lissé à
  partir de sa distance au pixel. Le dessin ne dépend plus d'aucune mémoire, et
  la taille de l'œil se règle comme le reste.

## 3. Comment c'est construit

**Le fond.** Pour chaque pixel, sa distance aux deux centres. Les cercles
d'une famille sont tous les `RI_PITCH` pixels ; on avance leur phase avec le
temps, donc ils grandissent. La distance au trait le plus proche, comparée à la
demi-épaisseur, donne directement la couverture du noir sur l'orange.

**L'œil.** L'amande est une *vesica* : deux arcs de cercle de même rayon, dont
les centres sont au-dessus et au-dessous de l'œil. La distance au bord, c'est
la plus grande des deux distances aux centres moins le rayon. Une amande pour
le contour noir, une autre plus petite pour le blanc. Dedans, des disques
concentriques : crème, iris noir, anneau crème, iris bleu, pupille, chacun
lissé sur sa distance au centre.

## 4. Les réglages

| Constante | Défaut | Effet |
| --- | --- | --- |
| `RI_SC` | `1.15` | taille de l'œil : 115 px de large, comme sur le mur. Toutes les formes de l'œil en dépendent. |
| `RI_PITCH` | `20` | écart entre deux cercles, en pixels. |
| `RI_LINE` | `3.0` | épaisseur des cercles, en pixels. |
| `RI_OFFSET` | `39` | distance des deux centres à l'œil, au-dessus et au-dessous. À zéro, une seule famille de cibles ; au-delà de 64, les centres sortent de l'écran. |
| `RI_FLOW_MS` | `3000` | le temps qu'un cercle met à avancer d'un écart. |
| `RI_SPIN_MS` | `5600` | un tour du reflet. |

Dans `CyberCycle/`, les mêmes constantes s'appellent `CE_…` (`CE_SC`,
`CE_PITCH`…).

## 5. Notes techniques

**La vitesse est estimée, pas mesurée.** Sur le mur, le T-QT tenait
42 images/s avec ses tables précalculées. Sans elles, il y a deux racines
carrées de plus par pixel pour le fond, et quatre ou cinq près de l'œil : sans
doute **30 à 40 images/s**. Le croquis plafonne à 40. Seul un flash le
confirmera ; si ça rame, on peut revenir aux tables, qui tiennent dans 64 ko.

**Une remarque de compilation.** L'amande se décrit par deux `float` et non par
une `struct` : l'IDE Arduino écrit les prototypes des fonctions avant les
`struct` du croquis, et une fonction qui en prend une ne compile pas.

**Pas d'état.** L'image ne dépend que de l'heure.
