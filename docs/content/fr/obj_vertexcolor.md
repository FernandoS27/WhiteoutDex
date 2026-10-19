# Wc3 Vertex Color

Le modificateur **Wc3 Vertex Color** colore tout un maillage. Vous pouvez animer la couleur, par exemple pour faire pulser un cristal ou faire briller une partie en rouge quand l'unité est touchée. L'exportateur l'écrit comme couleur de *geoset animation* du maillage.

Ajoutez-le depuis la liste des modificateurs : sélectionnez un maillage et choisissez **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Paramètres de base du modificateur.]]

| N° | Fonction |
|---|---|
| [[ctl:4]] | La couleur qui teinte le maillage. Le blanc laisse la texture telle quelle. Animez-la avec **Auto Key**. |
| [[ctl:7]] | Active l'indicateur *Ombre portée* (Drop Shadow) du maillage. |
| [[ctl:9]] | Utilise la couleur. Si cette option est désactivée, seul l'indicateur est écrit. |

!!! tip
    Pour faire apparaître et disparaître une partie en fondu, utilisez plutôt les clés de visibilité (*Générateur de clés de visibilité*) ou l'*Opacité* du Wc3Material.
