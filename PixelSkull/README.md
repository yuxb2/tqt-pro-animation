# PixelSkull — T-QT Pro

Une tête de mort de seize pixels de côté, agrandie en gros carrés, qui rebondit
sur les quatre bords de l'écran en claquant de la mâchoire. Deux couleurs, pas
une de plus.

Elle est **aussi la vue 13 de `CyberCycle/`**, et la première du tour : c'est
elle qui démarre quand la carte s'allume. Ce dossier-ci est le croquis
autonome, celui où on la travaille — ouvre `PixelSkull.ino` dans l'IDE avec les
réglages du README principal (§1). Rien à installer, il n'utilise que TFT_eSPI.

`preview.html` est le même code porté en JavaScript : ouvre-le dans un
navigateur pour régler les constantes sans reflasher. Le dessin, les formules et
le calage sur la grille y sont identiques à ceux du croquis.

---

## 1. Boutons

| Bouton | Action |
| --- | --- |
| **IO00** (BOOT) | taille du crâne : ×5 → ×4 → ×3 |
| **IO47** (KEY) | déplacement : rebond → dérive lente |

Dans `CyberCycle/`, où le bouton gauche sert à changer d'animation, c'est le
bouton droit qui fait passer les trois tailles.

---

## 2. Comment c'est dessiné

Tout part d'un dessin de 16 pixels de côté, **écrit en binaire dans le code**.
Chaque ligne se lit à l'œil, un `1` est un pixel d'os :

```c
0b1110011111100111,   // ###  ######  ###   orbites
0b1100001111000011,   // ##    ####    ##
```

C'est le seul endroit où le crâne existe. Pour lui changer la tête, on édite ces
treize lignes — il n'y a pas de générateur, pas de fichier d'image, rien à
relancer.

**L'agrandissement est entier.** Un pixel du dessin devient un carré de `SCALE`
pixels écran, et la position du crâne est calée sur cette même grille : il ne se
pose que sur des multiples de `SCALE`. Rien n'est donc jamais interpolé, et
l'escalier des contours reste exactement celui du dessin, quelle que soit la
taille. C'est aussi ce qui donne au déplacement son pas saccadé : le crâne
avance par carrés entiers, jamais par demi-pixels flous.

**La mâchoire n'est pas articulée.** C'est un second dessin de trois lignes, qui
descend d'un nombre entier de pixels de dessin. Bouche fermée, les dents du haut
et du bas se touchent et n'en font qu'une rangée ; ouverte, il reste un trou
noir entre les deux. Une rotation demanderait un rééchantillonnage, donc des
gris, donc la fin du noir et blanc.

**L'onde du claquement est un cosinus**, arrondi à l'entier. La bouche s'attarde
donc grande ouverte et bien fermée, et passe vite entre les deux — un vrai
claquement, pas un va-et-vient régulier.

---

## 3. La morsure (optionnelle, coupée)

Par défaut le claquement tourne à son rythme et le rebond au sien : la bouche
mâche, et le crâne se promène. `BITE_ON_WALL` accroche les deux — à chaque
impact contre un bord, la phase de la mâchoire est remise sur « grand ouvert » :

```c
if (hit && BITE_ON_WALL) chompT = CHOMP_S * 0.5f;
```

Une ligne, pas d'animation séparée, pas d'état en plus : la morsure tombe pile
sur l'impact et le crâne a l'air de réagir au mur. En échange le claquement perd
sa régularité, puisque sa phase est remise à zéro à chaque bord touché. C'est
pour ça qu'elle est laissée à `0` : le battement régulier a été jugé meilleur.
Mets `1` pour l'entendre mordre.

## 4. Les réglages

### La taille

| Constante | Défaut | Effet |
| --- | --- | --- |
| `SCALE_A` / `_B` / `_C` | `5` / `4` / `3` | les trois tailles que fait passer le bouton BOOT. **Toujours entières** : c'est ce qui garde les carrés carrés. |
| `GAP_MAX` | `3` | ouverture maximale, en pixels de dessin. |

> `GAP_MAX` fait deux choses. C'est l'ouverture, et c'est aussi la hauteur
> réservée en bas de l'écran : le crâne rebondit sur une boîte assez haute pour
> la bouche grande ouverte, **même bouche fermée**. Sans ça le rebond du bas
> changerait de place au rythme du claquement. En l'augmentant, on réduit donc
> le débattement vertical.

### Le mouvement

| Constante | Défaut | Effet |
| --- | --- | --- |
| `CHOMP_S` | `0.75` | durée d'un cycle ouvre-ferme, en secondes. |
| `BITE_ON_WALL` | `0` | la morsure à l'impact (§3). |
| `SPEED_X` / `SPEED_Y` | `14` / `15` | vitesses en px/s. Presque égales, mais pas tout à fait : le crâne part en diagonale et la diagonale se décale d'un tour à l'autre. Deux vitesses vraiment égales donneraient un aller-retour sur la même ligne. |
| `DRIFT_PX` / `DRIFT_PY` | `17` / `11.3` | les deux périodes du mode dérive, en secondes. Même règle. |

---

## 5. Notes techniques

**Le coût.** Une image, c'est un effacement du tampon et une centaine de carrés
remplis — au plus `16 × 16 × SCALE²` écritures, soit 6 400 à ×5. Il n'y a aucun
calcul par pixel d'écran, contrairement aux animations à phase du dépôt : c'est
l'envoi des 32 ko du tampon à l'écran qui domine, pas le dessin.

**La RAM.** 32 ko, uniquement le tampon d'écran. Le dessin lui-même tient dans
32 octets.

**Le rebond ne dérive pas.** Au lieu de replacer le crâne sur le bord touché, on
replie sa position par-dessus (`px = 2 * mx - px`). Le dépassement est ainsi
rendu au mouvement au lieu d'être jeté, et la trajectoire ne se met pas à glisser
au fil des heures.
