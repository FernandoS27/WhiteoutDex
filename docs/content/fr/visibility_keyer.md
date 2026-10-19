# Générateur de clés de visibilité

Dans Warcraft III, vous affichez et masquez des parties d'un modèle avec des **clés de visibilité**. Par exemple, une arme qui n'apparaît que dans l'animation d'attaque, ou une lueur qui n'apparaît que pendant que l'unité lance un sort. Le générateur de clés de visibilité pose ces clés pour vous.

Ouvrez-le avec **WhiteoutDex > [[t:menu_visibility_keyer]]**. Sélectionnez d'abord les objets.

[[shot:visibility_keyer|Le générateur de clés de visibilité.]]

| N° | Fonction |
|---|---|
| [[ctl:3]] | Affiche ou masque les objets sélectionnés à l'image actuelle. |
| [[ctl:5]] | Ajoute un contrôleur de visibilité à tous les objets sélectionnés. |
| [[ctl:6]] | Retire les contrôleurs de visibilité des objets sélectionnés. |
| [[ctl:7]] | Copie les clés de visibilité de l'objet sélectionné. |
| [[ctl:8]] | Les colle sur les objets sélectionnés. |
| [[ctl:9]] | La façon dont le jeu passe d'une clé à l'autre : *None* change d'un coup, les autres font un fondu. |
| [[ctl:11]] | Donne aux contrôleurs existants le type choisi. |
| [[ctl:13]] | Pose une clé *afficher* à l'image actuelle. |
| [[ctl:14]] | Pose une clé *masquer* à l'image actuelle. |
| [[ctl:16]] | Rend l'objet visible sur toute la plage d'animation. |
| [[ctl:17]] | Masque l'objet sur toute la plage d'animation. |
| [[ctl:19]] | Affiche l'objet dans les séquences sélectionnées dans le *gestionnaire de séquences*. |
| [[ctl:20]] | Masque l'objet dans les séquences sélectionnées. |

## Exemple : une arme seulement pour l'attaque

1. Sélectionnez l'arme.
2. Cliquez sur [[ctl:17]] pour la masquer partout.
3. Dans le *gestionnaire de séquences*, sélectionnez les séquences *Attack*.
4. Cliquez sur [[ctl:19]].

!!! tip
    Utilisez l'interpolation *None* pour la plupart des clés de visibilité. Avec les autres types, le jeu fait apparaître et disparaître l'objet en fondu. Pour la plupart des parties, ce n'est pas le bon rendu.
