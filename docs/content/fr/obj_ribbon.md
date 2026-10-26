# Émetteur de traînée

Un émetteur de traînée (ribbon) dessine une **traînée** derrière un point en mouvement : la traînée lumineuse d'un coup d'épée, la queue d'une comète, la fumée d'une fusée. Liez-le à l'os qui bouge, par exemple l'arme.

Créez-le avec **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Ribbon Material Parameters

[[shot:obj_ribbon_r1|Ribbon Material Parameters.]]

| N° | Fonction |
|---|---|
| [[ctl:1079]] | Le matériau de la traînée (un *Wc3Material*). Cliquez pour le placer dans le Material Editor. |
| [[ctl:1073]] | La texture est une grille d'images : nombre de lignes (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … et de colonnes (1, 2, 4, 8 …). |
| [[ctl:1077]] | L'image de la grille que montre la traînée. Animez cette valeur pour faire scintiller la traînée. |

## Ribbon Properties

[[shot:obj_ribbon_r2|Ribbon Properties.]]

| N° | Fonction |
|---|---|
| [[ctl:1066]] | La couleur de la traînée. |
| [[ctl:1059]] | La distance que couvre la traînée au-dessus de l'émetteur. |
| [[ctl:1067]] | La distance qu'elle couvre en dessous de l'émetteur. Au-dessus + en dessous = largeur de la traînée. |
| [[ctl:1080]] | La transparence : 0 = invisible, 1 = opaque. Animez-la jusqu'à 0 pour masquer la traînée. |
| [[ctl:1069]] | Le nombre de morceaux de traînée émis par seconde. Plus de morceaux donnent une traînée plus lisse. |
| [[ctl:1071]] | La durée de vie d'un morceau, en secondes. Une vie plus longue donne une traînée plus longue. |
| [[ctl:1082]] | Attire la traînée vers le bas. |
| [[ctl:1090]] | **Contrôleur :** la façon dont une valeur animée change entre les clés. Chaque valeur a son propre contrôleur. |

!!! tip
    Pour une traînée d'épée : l'émetteur sur la lame, un matériau *Additive*, un alpha à 0 sauf pendant le coup, une durée de vie d'environ 0.2 seconde.
