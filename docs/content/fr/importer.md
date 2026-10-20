# L'importateur

L'importateur charge un modèle Warcraft III (`.mdx` ou `.mdl`) dans 3ds Max. Il charge aussi les textures du modèle depuis votre jeu.

## Ouvrir l'importateur

Utilisez l'une de ces méthodes :

- Le menu **WhiteoutDex > [[t:menu_importer]]**
- Le bouton **Import** de la barre latérale WhiteoutDex
- **File > Import**, puis choisissez le type de fichier *Warcraft III MDX/MDL Model*

Choisissez un fichier. La fenêtre d'import s'ouvre alors.

## La fenêtre d'import

[[shot:importer|La fenêtre d'import avec l'onglet Objets.]]

Le haut de la fenêtre indique le nom du fichier et la version de jeu du modèle (*Classic* ou *Reforged*). Il indique aussi le nombre d'os, de maillages (geosets) et d'animations.

| N° | Fonction |
|---|---|
| [[ctl:1052]] | **Réglages rapides.** Choisissez un préréglage. Il règle pour vous tous les interrupteurs ci-dessous. Choisissez *Tout* pour tout importer. Les autres préréglages importent moins, par exemple seulement le maillage sans les matériaux. |
| [[ctl:1059]] | Supprime la scène ouverte et importe le modèle dans une scène vide. |
| [[ctl:1060]] | Ajoute le modèle à la scène ouverte. Utilisez cette option pour combiner des modèles. |
| [[ctl:1061]] | Affiche les objets du modèle (cette image). |
| [[ctl:1062]] | Affiche les options d'animation. |
| [[ctl:1063]] | Affiche les options du maillage, des matériaux et des textures. |
| [[ctl:1021]] | Interrupteur pour tous les objets. S'il est désactivé, aucun objet n'est importé. |
| [[ctl:1022]] | Le squelette. Il est nécessaire pour les animations et les maillages skinnés. |
| [[ctl:1023]] | Les objets d'aide. Ils déplacent d'autres objets, mais ils sont invisibles dans le jeu. |
| [[ctl:1024]] | Les lumières. |
| [[ctl:1025]] | Les points d'attache, par exemple pour les armes et les effets de sort. |
| [[ctl:1028]] | Les objets d'événement, par exemple les sons, les empreintes de pas et le sang. |
| [[ctl:1026]] | Particle Emitter 1 : des émetteurs qui génèrent de petits modèles. |
| [[ctl:1027]] | Particle Emitter 2 : des émetteurs pour le feu, la fumée, les étincelles et les effets semblables. |
| [[ctl:1029]] | Les émetteurs de traînées : des traînées, par exemple derrière une épée. |
| [[ctl:1030]] | Les formes de collision (boîtes et sphères). Le jeu s'en sert pour savoir où vous pouvez cliquer sur le modèle. |
| [[ctl:1031]] | Les caméras, par exemple la caméra de portrait. |
| [[ctl:1]] | Lance l'import. |
| [[ctl:2]] | Ferme la fenêtre sans importer. |

Le nombre à côté de chaque type d'objet indique combien le modèle en contient. Les types absents du modèle sont grisés.

!!! note
    Les modèles Reforged peuvent avoir deux types d'objets de plus : les émetteurs *Popcorn FX* et *FaceFX*. Vous ne les voyez que si le modèle en contient.

### Onglet Animations

[[shot:importer_animations|L'onglet Animations.]]

| N° | Fonction |
|---|---|
| [[ctl:1041]] | Interrupteur pour toutes les animations. S'il est désactivé, le modèle arrive sans mouvement. |
| [[ctl:1042]] | Le déplacement (clés de position). |
| [[ctl:1043]] | Les clés de rotation. |
| [[ctl:1044]] | Les clés d'échelle. |
| [[ctl:1048]] | Les clés d'affichage et de masquage. |
| [[ctl:1045]] | Les valeurs animées des objets, par exemple l'intensité d'une lumière ou le taux d'émission des particules. |
| [[ctl:1046]] | Les coordonnées de texture en mouvement (animation UV). |
| [[ctl:1047]] | Les changements de texture dans le temps (flipbook). |
| [[ctl:1049]] | Les changements de couleur dans le temps. |

### Onglet Modèle

[[shot:importer_model|L'onglet Modèle.]]

| N° | Fonction |
|---|---|
| [[ctl:1001]] | Importe le skin : quel os déplace quel sommet. Sans lui, le maillage ne suit pas les os. |
| [[ctl:1011]] | Crée des matériaux WhiteoutDex pour le modèle. |
| [[ctl:1012]] | Charge les textures. WhiteoutDex cherche dans les dossiers de votre jeu (voir *Paramètres*). |
| [[ctl:1071]] | Importe les aides comme objets *Point*. Si l'option est désactivée, les aides arrivent comme des os. |
| [[ctl:1081]] | Soude les sommets qui sont au même endroit et simplifie le maillage. |
| [[ctl:1082]] | Supprime les os et les aides qui n'ont pas d'animation et ne servent à rien. |

## D'où viennent les textures

WhiteoutDex cherche chaque texture dans cet ordre :

1. Dans le dossier du modèle et ses sous-dossiers.
2. Dans les archives du jeu. Pour les modèles classiques (version 800), il cherche d'abord dans les archives **MPQ**, puis dans **CASC**. Pour les modèles Reforged, c'est l'inverse.

La ligne du bas de la fenêtre indique les archives utilisées. Vous définissez les dossiers du jeu dans [[t:menu_settings]].

!!! tip
    L'importateur enregistre à côté du fichier du modèle les textures qu'il prend dans les archives du jeu. La fois suivante, il les trouve donc tout de suite.

## Bon à savoir

- L'importateur mémorise vos choix pour la fois suivante.
- Pour importer un modèle du jeu sans fichier sur votre disque, voir *Importer depuis le jeu*.
