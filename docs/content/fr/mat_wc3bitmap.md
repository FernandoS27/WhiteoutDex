# Wc3Bitmap

Le **Wc3Bitmap** est la map de texture de WhiteoutDex. Placez-le dans les emplacements d'un *Wc3Material*. Il lit les fichiers `.blp`, `.dds`, `.tga`, `.png` et les autres formats d'image de 3ds Max. Il contient aussi l'animation de texture (animation UV) et le chemin de la texture dans le jeu.

## Paramètres de texture WC3

[[shot:mat_wc3bitmap_r1|Paramètres de texture WC3.]]

| N° | Fonction |
|---|---|
| [[ctl:6]] | Choisissez un fichier image sur votre disque. |
| [[ctl:7]] | Choisissez une texture dans les archives du jeu. |
| [[ctl:9]] | Utilise une texture que le jeu remplit (couleur d'équipe, halo d'équipe, falaise, arbres). |
| [[ctl:12]] | La texture se répète sur la largeur. |
| [[ctl:13]] | La texture se répète sur la hauteur. |
| [[ctl:14]] | Utilise la texture comme réflexion (sphere environment map). |

## Animation UV (TXAN)

[[shot:mat_wc3bitmap_r2|Animation UV.]]

Animez ces valeurs pour faire bouger la texture. Le *Créateur d'animation de texture* peut les régler pour vous.

| N° | Fonction |
|---|---|
| [[ctl:3]] | La façon dont les valeurs changent entre les clés. |
| [[ctl:8]] | Déplace la texture sur le côté (U). |
| [[ctl:11]] | Déplace la texture vers le haut et le bas (V). |
| [[ctl:14]] | Le décalage W. |
| [[ctl:17]] | Fait tourner la texture. |
| [[ctl:20]] | Répète la texture plus ou moins souvent sur le côté. |
| [[ctl:23]] | Répète la texture plus ou moins souvent vers le haut et le bas. |

## Chemin de préfixe

[[shot:mat_wc3bitmap_r3|Chemin de préfixe.]]

Le préfixe est le dossier que l'exportateur écrit devant le nom de la texture, par exemple `Textures\` ou `war3mapImported\`.

| N° | Fonction |
|---|---|
| [[ctl:5]] | Choisissez un préfixe enregistré. |
| [[ctl:6]] | Modifiez la liste des préfixes enregistrés. |

!!! tip
    Les textures d'une carte doivent utiliser le préfixe de cette carte. Pour les modèles personnalisés dans les cartes, c'est en général `war3mapImported\`.
