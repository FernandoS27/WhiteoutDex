# Tutoriel : animer

Dans cette leçon, vous donnez deux animations à la caisse de la leçon précédente : *Stand* (elle ne fait rien) et *Death* (elle tombe en morceaux).

## Étape 1 : planifier la ligne de temps

Toutes les animations d'un modèle partagent une seule ligne de temps. Chaque animation reçoit sa propre partie :

| Séquence | Images |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Laissez quelques images libres entre les parties.

## Étape 2 : créer les séquences

1. Ouvrez **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Cliquez sur **Nouveau...**, choisissez *Stand*, réglez le début sur 0 et la fin sur 80, puis cliquez sur **OK**.
3. Cliquez sur **Nouveau...**, choisissez *Death*, réglez le début sur 100 et la fin sur 164, activez **Sans boucle**, puis cliquez sur **OK**.

![Le gestionnaire de séquences avec une séquence sélectionnée.](sequence_manager_selected.png)

## Étape 3 : poser des clés

1. Activez **Auto Key** dans 3ds Max.
2. Posez des clés au début et à la fin de chaque séquence : ouvrez **WhiteoutDex > [[t:menu_anim_general]]**, sélectionnez la caisse et cliquez sur **Ajouter des clés de début et de fin aux objets sélectionnés**.
3. Allez à l'image 130. Faites pivoter la caisse et descendez-la, comme si elle tombait.
4. Allez à l'image 164 et placez-la sous le sol.
5. Désactivez **Auto Key**.

## Étape 4 : tester

1. Dans le gestionnaire de séquences, double-cliquez sur *Death*. 3ds Max la lit.
2. Ouvrez l'aperçu (**WhiteoutDex > [[t:menu_renderer]]**) et choisissez *Death* dans la liste **Animation**.

!!! tip
    Les os s'animent mieux que les maillages : liez les parties d'un modèle à des os et animez les os. Ensuite, appliquez un modificateur **Skin** aux maillages.

!!! note
    Le jeu lit *Stand* en boucle. *Death* est lue une seule fois. Ensuite, le corps reste en place jusqu'aux animations *Decay* (si le modèle en a).
