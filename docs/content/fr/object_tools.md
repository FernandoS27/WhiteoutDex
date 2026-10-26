# Outils de manipulation d'objets

Copiez et collez des positions, des liens et des positions de sommets entre objets. Pratique pour les modèles symétriques : construisez le côté gauche, puis copiez-le sur le côté droit.

Ouvrez les outils avec **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|Les outils de manipulation d'objets avec un maillage sélectionné.]]

## Objets

| N° | Fonction |
|---|---|
| [[ctl:5]] | **Hiérarchie :** enregistre les liens de parenté des objets sélectionnés et les délie. |
| [[ctl:6]] | Relie les objets à leurs parents enregistrés. |
| [[ctl:8]] | **Position :** copie la position du premier objet sélectionné. |
| [[ctl:9]] | Déplace les objets sélectionnés à la position copiée. |
| [[ctl:11]] | **Structure (G↔D) :** copie les positions de tous les objets sélectionnés avec leurs noms. |
| [[ctl:12]] | Les colle sur les objets au nom symétrique : une position copiée depuis `Arm_L` va sur `Arm_R`. |
| [[ctl:14]] | Délie les objets un moment pendant le collage, pour que leurs enfants ne bougent pas avec eux. |
| [[ctl:15]] | Applique un miroir à la position pendant le collage, selon les axes cochés : [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Sommets (Outils Poly)

Sélectionnez d'abord des sommets d'un Editable Poly ou d'un Editable Mesh. La ligne au-dessus des boutons indique l'objet et le nombre de sommets sélectionnés.

| N° | Fonction |
|---|---|
| [[ctl:22]] | Copie les positions des sommets sélectionnés. |
| [[ctl:23]] | Colle les positions sur les sommets. |
| [[ctl:26]] | Inverse les valeurs X pendant le collage. [[ctl:27]] et [[ctl:28]] font de même pour Y et Z. |
| [[ctl:29]] | Applique le miroir autour du centre du monde. |
| [[ctl:30]] | Applique le miroir autour du pivot de l'objet. |
| [[ctl:31]] | Applique le miroir autour du centre de la sélection. |
| [[ctl:35]] | Colle sur les mêmes numéros de sommets. Utilisez ce mode sur le même maillage ou sur une copie. |
| [[ctl:36]] | Colle sur les sommets sélectionnés, même sur un autre maillage. |

!!! tip
    Nommez vos objets avec `_L` et `_R` (ou `Left` et `Right`). *Structure (G↔D)* trouve alors seul le partenaire de chaque objet.
