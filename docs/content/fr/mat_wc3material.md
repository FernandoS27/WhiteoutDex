# Wc3Material

Chaque maillage que vous exportez a besoin d'un **Wc3Material**. Il contient les textures et indique au jeu comment les dessiner. L'importateur crée les Wc3Material pour vous. Pour vos propres maillages, créez-en un dans le Material Editor ou convertissez vos matériaux avec la *Correction des matériaux*.

Les images montrent un matériau du modèle d'exemple dans le Compact Material Editor.

## Paramètres de base du matériau

[[shot:mat_wc3material_r1|Paramètres de base du matériau.]]

| N° | Fonction |
|---|---|
| [[ctl:3]] | **Shader.** *SD* pour les modèles classiques, *HD* pour Reforged, *SD on HD* pour des textures classiques dans un modèle Reforged, *Crystal* pour le shader cristal de Reforged. |
| [[ctl:5]] | **Mode de filtrage** – la façon dont le matériau est dessiné (voir le tableau ci-dessous). |
| [[ctl:8]] | Les deux côtés des faces sont dessinés. |
| [[ctl:9]] | Ignore la lumière. Idéal pour les parties lumineuses. |
| [[ctl:10]] | Ignore le brouillard. |
| [[ctl:11]] | Une seule couleur pour tout le maillage. |
| [[ctl:12]] | Active l'indicateur *Pleine résolution* (Full Resolution) du matériau. |
| [[ctl:13]] | Dessiné par-dessus les autres éléments. |
| [[ctl:14]] | Ne cache pas ce qui se trouve derrière. |
| [[ctl:15]] | Le joueur ne peut pas cliquer sur cette partie. |
| [[ctl:16]] | Utilise l'occlusion ambiante (Reforged). |
| [[ctl:17]] | Les faces arrière projettent des ombres (Reforged). |
| [[ctl:18]] | Pas éclairé du tout (Reforged). |
| [[ctl:20]] | L'ordre de dessin des calques transparents : du plus proche au plus loin, ou du plus loin au plus proche. |
| [[ctl:24]] | L'ordre de dessin entre les matériaux (−20 à 20). Une valeur plus élevée est dessinée plus tard. |
| [[ctl:26]] | Utilise une texture que le jeu remplit : couleur d'équipe, halo d'équipe, falaise ou l'une des textures d'arbre. |

| Mode de filtrage | Aspect | Usage |
|---|---|---|
| Aucun | Opaque. | Les parties normales. |
| Transparent | Des trous là où l'alpha est faible, bords nets. | Feuilles, cheveux, clôtures. |
| Fondu | Transparence douce. | Verre, bords doux, ombres. |
| Additif | Ajoute de la lumière, brille. Le noir est invisible. | Feu, magie, rayons de lumière. |
| AddAlpha | Ajoute de la lumière, utilise l'alpha. | Lueurs avec une forme. |
| Modulation | Assombrit ce qui est derrière. | Marques sombres, ombres. |
| Modulation 2x | Assombrit ou éclaircit. | Effets spéciaux. |

## Animation du calque

[[shot:mat_wc3material_r2|Animation du calque.]]

| N° | Fonction |
|---|---|
| [[ctl:6]] | L'opacité du calque (0 à 1). Animez-la pour faire apparaître et disparaître le matériau en fondu. |
| [[ctl:7]] | La façon dont l'opacité change entre les clés. |
| [[ctl:9]] | Écrit l'animation d'opacité dans le modèle. |
| [[ctl:11]] | La texture change par étapes dans le temps, comme un flipbook. |
| [[ctl:14]] | Le canal UV utilisé par la texture. |
| [[ctl:15]] | Écrit l'animation de changement de texture dans le modèle. |

Les textures en mouvement (animation UV) se règlent sur le *Wc3Bitmap*, pas ici.

## Textures du matériau

[[shot:mat_wc3material_r3|Textures du matériau.]]

| N° | Fonction |
|---|---|
| [[ctl:4]] | **Diffuse :** la texture de couleur. Les modèles classiques n'utilisent que celle-ci. |
| [[ctl:5]] | Ouvre l'explorateur de textures pour cet emplacement (textures du jeu). |
| [[ctl:10]] | **Normal :** les détails de surface (Reforged). |
| [[ctl:16]] | **ORM :** occlusion, rugosité et métal dans une seule texture (Reforged). |
| [[ctl:22]] | **Émissive :** les parties qui brillent d'elles-mêmes (Reforged). |
| [[ctl:28]] | **Couleur d'équipe :** un masque pour la couleur d'équipe (Reforged, facultatif). |
| [[ctl:34]] | **Environnement :** la texture de réflexion (Reforged). |

Chaque emplacement a un **Préfixe de chemin** : le dossier que l'exportateur écrit devant le nom du fichier, par exemple `Textures\`.

## Paramètres Reforged

[[shot:mat_wc3material_r4|Paramètres Reforged.]]

| N° | Fonction |
|---|---|
| [[ctl:5]] | L'intensité de la lueur de la texture émissive. |
| [[ctl:9]] | La couleur de Fresnel (lumière de bord) : rouge … |
| [[ctl:12]] | … vert … |
| [[ctl:15]] | … et bleu. |
| [[ctl:18]] | L'intensité de la lumière de bord. |
| [[ctl:21]] | La part de couleur d'équipe utilisée par la lumière de bord. |
