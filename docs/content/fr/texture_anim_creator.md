# Créateur d'animation de texture

Cet outil fait bouger les textures : de l'eau qui coule, un feu tiré d'une feuille de sprites, ou un flipbook à partir de nombreuses images. L'outil écrit les clés pour vous.

Ouvrez-le avec **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

Il existe trois types d'animation :

| Type | Usage |
|---|---|
| **Anim flux** | La texture glisse, tourne ou grandit : eau, lave, énergie. |
| **Séq. (unique)** | Une image qui contient de nombreuses petites images dans une grille (une *feuille de sprites*). L'outil les montre l'une après l'autre. |
| **Séq. (multiple)** | Un dossier avec de nombreuses images. L'outil les montre l'une après l'autre (un *flipbook*). |

[[shot:texture_anim_creator|Le créateur d'animation de texture avec Séq. (unique).]]

## Choisir la texture

| N° | Fonction |
|---|---|
| [[ctl:4]] | Cliquez sur un maillage dans le viewport pour utiliser son matériau. |
| [[ctl:5]] | Utilise le maillage sélectionné. |
| [[ctl:6]] | Utilise le matériau actif dans le Material Editor. |
| [[ctl:8]] | Le matériau. |
| [[ctl:11]] | La texture du matériau. La ligne en dessous indique son nom et sa taille. |
| [[ctl:14]] | Anim flux. |
| [[ctl:15]] | Séq. (unique). |
| [[ctl:16]] | Séq. (multiple). |

## Séq. (unique) : une feuille de sprites

| N° | Fonction |
|---|---|
| [[ctl:46]] | L'image où l'animation commence. |
| [[ctl:50]] | Le nombre de répétitions de l'animation. |
| [[ctl:54]] | Le nombre d'images dans une ligne de la feuille. |
| [[ctl:58]] | Le nombre de lignes de la feuille. |
| [[ctl:62]] | La première image à montrer. |
| [[ctl:66]] | La dernière image à montrer. |
| [[ctl:68]] | Saute des images entre deux vignettes (0 = chaque image montre une nouvelle vignette). |
| [[ctl:71]] | En avant ou en arrière. |
| [[ctl:74]] | Vignettes par seconde : 10, 15, 30 ou 60. |
| [[ctl:77]] | **OUI :** l'animation tourne en boucle en permanence comme séquence globale. **NON :** elle se déroule sur la ligne de temps. |
| [[ctl:80]] | **OUI :** garde les clés que la texture a déjà. **NON :** les remplace. |
| [[ctl:84]] | Trouve seul le nombre de colonnes et de lignes de l'image. |
| [[ctl:85]] | Règle les coordonnées UV du maillage sur une vignette de la feuille. Une seule fois suffit. |
| [[ctl:87]] | **OUI :** réduit le modificateur UV dans le maillage après *Générer la carte UV*. |
| [[ctl:116]] | Écrit l'animation. La ligne au-dessus indique le nombre de clés et la durée. |

## Anim flux

[[shot:texture_anim_flow|Anim flux.]]

1. Réglez le début et la fin.
2. Cliquez sur les flèches de la direction dans laquelle la texture se déplace. Vous pouvez aussi la faire tourner (gauche, droite) et la faire grandir ou rétrécir (+, −).
3. Réglez *Animation globale* sur OUI pour une texture qui bouge en permanence.
4. Cliquez sur **Générer l'animation**.

## Séq. (multiple)

[[shot:texture_anim_many|Séq. (multiple).]]

1. Choisissez le dossier des images (bouton **...**). Les images sont utilisées dans l'ordre de leurs noms.
2. Réglez l'ordre de lecture, la vitesse et l'image de début.
3. Réglez le préfixe du chemin, par exemple `Textures\`. Le modèle utilise ce chemin pour chaque image.
4. Cliquez sur **Générer l'animation**.

!!! tip
    Les clés de texture se trouvent sur le *Wc3Bitmap* (animation UV) ou sur le *Wc3Material* (flipbook). C'est là qu'il faut regarder pour les modifier plus tard.
