# Point d'attache

Un point d'attache marque un endroit du modèle. Le jeu y attache des éléments : l'arme d'un héros, un effet de sort sur la main, la flèche qui touche la poitrine, la barre de vie au-dessus de la tête.

Créez-le avec **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Liez-le à l'os qu'il doit suivre.

Dans le viewport, chaque point d'attache affiche son nom en jaune.

## Fonctions générales

[[shot:obj_attachpoint_r1|Fonctions générales.]]

| N° | Fonction |
|---|---|
| [[ctl:3]] | Cliquez ici, puis cliquez sur un objet dans le viewport. Le point d'attache se place sur cet objet et se lie à lui. |

## Paramètres de la map du modèle

[[shot:obj_attachpoint_r2|Paramètres de la map du modèle.]]

Un point d'attache peut porter son propre modèle, par exemple une épée que l'unité tient toujours.

| N° | Fonction |
|---|---|
| [[ctl:9]] | Choisissez le fichier de modèle sur votre disque. |
| [[ctl:10]] | Choisissez un modèle dans les archives du jeu. |

Les deux champs au-dessus des boutons contiennent le dossier et le nom du fichier. Le chemin est relatif au dossier de Warcraft III, par exemple `Textures\Model.mdx`.

## Paramètres du point d'attache

[[shot:obj_attachpoint_r3|Paramètres du point d'attache.]]

Le jeu trouve un point d'attache grâce à son **nom**. Vous composez ce nom à partir de quatre parties :

| N° | Fonction |
|---|---|
| [[ctl:5]] | **Type :** l'emplacement principal : *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* ou *Sprite*. |
| [[ctl:8]] | **Monture :** pour les cavaliers sur une monture. |
| [[ctl:11]] | **Côté :** *Left* ou *Right*, pour les mains et les pieds. |
| [[ctl:14]] | **Extra :** des mots supplémentaires, par exemple *Rear*, *Alternate*, *RallyPoint*, *Gold*, ou *First* … *Sixth* pour les bâtiments. |
| [[ctl:22]] | Utilise le modèle des *Paramètres de la map du modèle*. |
| [[ctl:26]] | La visibilité (0 à 1). Animez-la pour afficher ou masquer le modèle attaché. |
| [[ctl:27]] | Indique par rapport à quoi le chemin du modèle est relatif. |
| [[ctl:34]] | Un numéro pour le point d'attache. |
| [[ctl:36]] | Écrit ce numéro dans le modèle. |

Exemple : Type *Hand* et Côté *Right* donnent le nom `Hand Right Ref`. Le jeu y place un effet de sort pour la main droite.

!!! tip
    L'outil *Points d'attache en un clic* des *Anim. : outils généraux* crée pour vous les points d'attache habituels, si vos os ont des noms connus.
