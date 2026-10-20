# Émetteur de particules 2

Le Particle Emitter 2 crée la plupart des effets de Warcraft III : feu, fumée, étincelles, poussière, magie. Il émet de nombreuses petites images plates (les particules). Chaque particule vit peu de temps. Pendant sa vie, elle change de couleur, de taille et de transparence.

Créez-le avec **Create > Geometry > Particle Systems > Wc3Particles2**. WhiteoutDex montre les particules directement dans le viewport.

Les images ci-dessous montrent l'émetteur du modèle d'exemple *HeroPaladin*.

## Particle Configuration

[[shot:obj_particles2_r1|Particle Configuration.]]

| N° | Fonction |
|---|---|
| [[ctl:1030]] | Charge tous les réglages depuis un fichier `.ini`. |
| [[ctl:1031]] | Enregistre tous les réglages dans un fichier `.ini`. Utilisez cette fonction pour copier un effet sur un autre modèle. |
| [[ctl:1032]] | Si cette option est activée, *Importer* charge aussi les clés animées des valeurs, et pas seulement les valeurs fixes. |

## Texture Options

[[shot:obj_particles2_r2|Texture Options.]]

| N° | Fonction |
|---|---|
| [[ctl:1047]] | Le dossier de la texture dans le jeu, par exemple `Textures\`. |
| [[ctl:3075]] | Le nom de fichier de la texture. |
| [[ctl:1033]] | Choisissez un fichier de texture sur votre disque. |
| [[ctl:1034]] | Choisissez une texture dans les archives du jeu. |
| [[ctl:1035]] | La façon dont les particules sont dessinées : *Blend* (transparence normale), *Additive* (brille, idéal pour le feu et la magie), *Modulate*, *Modulate 2X* (assombrit) ou *Alpha Key* (bords nets). |
| [[ctl:4113]] | Les particules ignorent la lumière. |
| [[ctl:4115]] | Les particules ignorent le brouillard. |
| [[ctl:4112]] | Trie les particules par distance. Utilisez cette option quand des particules transparentes se superposent mal. |
| [[ctl:4114]] | Utilise la couleur d'équipe ou le halo d'équipe comme texture. |
| [[ctl:3094]] | La texture est une grille d'images : nombre de lignes … |
| [[ctl:3096]] | … et de colonnes. La particule montre les images l'une après l'autre. |

## Emitter Options

[[shot:obj_particles2_r3|Emitter Options.]]

| N° | Fonction |
|---|---|
| [[ctl:3048]] | La vitesse à laquelle les particules s'éloignent. |
| [[ctl:3050]] | La variation de vitesse d'une particule à l'autre (0 = toutes identiques). |
| [[ctl:3052]] | L'angle du cône dans lequel les particules volent. Un petit angle donne un faisceau étroit, un grand angle une large gerbe. |
| [[ctl:3057]] | Attire les particules vers le bas (ou vers le haut avec une valeur négative). |
| [[ctl:1040]] | **Contrôle :** la façon dont une valeur animée change entre les clés (voir *Glossaire*). Chaque valeur a son propre contrôle. |

## Timing Options

[[shot:obj_particles2_r4|Timing Options.]]

| N° | Fonction |
|---|---|
| [[ctl:3042]] | La durée de vie d'une particule, en secondes. |
| [[ctl:3053]] | Le nombre de particules émises par seconde. Animez-le jusqu'à 0 pour arrêter l'effet. |
| [[ctl:1020]] | Émet toutes les particules d'un coup à une clé, comme une explosion. |

## Size Options

[[shot:obj_particles2_r5|Size Options.]]

| N° | Fonction |
|---|---|
| [[ctl:3044]] | La largeur de la zone où les particules apparaissent. |
| [[ctl:3046]] | La longueur de cette zone. |
| [[ctl:1028]] | Les particules apparaissent le long d'une ligne au lieu d'une zone. |

## Particle Options

[[shot:obj_particles2_r6|Particle Options.]]

| N° | Fonction |
|---|---|
| [[ctl:1023]] | La particule est une image plate (tête). |
| [[ctl:1024]] | La particule est une traînée étirée derrière son mouvement (queue). |
| [[ctl:1025]] | La tête et la queue. |
| [[ctl:3073]] | La longueur de la queue. |
| [[ctl:6027]] | Le moment de sa vie où la particule atteint les valeurs du *milieu* (0.5 = la moitié de sa vie). |
| [[ctl:6033]] | La couleur au début, au milieu et à la fin de la vie. Cliquez sur une case pour choisir une couleur. |
| [[ctl:6037]] | La transparence au début, au milieu et à la fin : 0 = invisible, 255 = opaque. |
| [[ctl:6041]] | La taille au début, au milieu et à la fin. |
| [[ctl:6045]] | Les images de la grille de texture que la tête montre pendant sa vie … |
| [[ctl:6049]] | … et pendant son déclin. |
| [[ctl:6053]] | La même chose pour la queue pendant sa vie … |
| [[ctl:6057]] | … et pendant son déclin. |

## Other Options

[[shot:obj_particles2_r7|Other Options.]]

| N° | Fonction |
|---|---|
| [[ctl:3059]] | L'ordre de dessin. Les valeurs plus élevées sont dessinées plus tard (par-dessus). |
| [[ctl:4116]] | Les particules bougent avec le modèle. Sans cette option, elles restent là où elles sont apparues. |
| [[ctl:4117]] | Les particules sont posées à plat sur le sol au lieu de faire face à la caméra. Idéal pour les anneaux et les ondes. |

!!! tip
    Pour le feu et la magie, utilisez *Additive*, un petit angle de cône et une courte durée de vie. Faites passer l'alpha de 0 à 255 puis de nouveau à 0. Les particules apparaissent et disparaissent alors en douceur.
