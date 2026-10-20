# Objet d'événement

Un objet d'événement déclenche quelque chose à un moment précis d'une animation : un son quand l'épée frappe, une empreinte quand le pied touche le sol, du sang quand l'unité meurt, un modèle qui apparaît.

Créez-le avec **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Placez-le là où l'événement doit se produire. Liez-le à un os s'il doit bouger.

Dans le viewport, chaque objet d'événement affiche son nom en vert.

[[shot:obj_event_r2|Le panneau Objet d'événement d'un son de mort du modèle d'exemple.]]

## Choisir l'événement

| N° | Fonction |
|---|---|
| [[ctl:5]] | Le type d'événement (voir le tableau ci-dessous). |
| [[ctl:8]] | L'événement exact, pris dans la liste du jeu, par exemple un son ou une couleur de sang. |
| [[ctl:13]] | Applique votre choix et nomme l'objet. La ligne sous le bouton indique le code de l'événement. |

Vous pouvez aussi saisir vous-même un code d'événement dans *Données personnalisées*.

| Type | Le code commence par | Ce qui se passe |
|---|---|---|
| FootPrint | `FPT` | Une empreinte de pas sur le sol. |
| BloodSplat | `SPL` | Une éclaboussure sur le sol, par exemple du sang. |
| UberSplat | `UBR` | Une grande marque au sol, par exemple un cratère ou la base d'un bâtiment. |
| Sound | `SND` | Un son. |
| SpawnObject | `SPN` | Un modèle apparaît, par exemple une partie du corps qui tombe. |

## Quand l'événement se produit

| N° | Fonction |
|---|---|
| [[ctl:19]] | Les instants où l'événement se produit. |
| [[ctl:20]] | Placez le curseur temporel sur la bonne image, puis cliquez ici pour ajouter un instant. |
| [[ctl:21]] | Supprime l'instant sélectionné. |
| [[ctl:22]] | Supprime tous les instants. |

!!! tip
    Utilisez le *gestionnaire de séquences* pour aller à l'animation. Déplacez ensuite le curseur image par image pour trouver le moment exact, par exemple quand le pied touche le sol.
