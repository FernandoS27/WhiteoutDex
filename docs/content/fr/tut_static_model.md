# Tutoriel : votre premier modèle

Dans cette leçon, vous créez un objet simple à partir de rien : une caisse, par exemple un élément de décor (doodad) pour une carte.

## Étape 1 : modéliser la caisse

1. Créez une **Box** : *Create > Geometry > Box*. Donnez-lui environ 60 × 60 × 60 unités. Une unité dans 3ds Max vaut une unité dans le jeu.
2. Placez-la pour qu'elle repose sur le sol (le dessous à la hauteur 0) avec son centre en 0, 0.

## Étape 2 : lui donner un matériau

1. Ouvrez le Material Editor ([[key:M]]).
2. Choisissez un emplacement libre, cliquez sur le bouton du type de matériau et choisissez **Wc3Material**.
3. Dans *Textures du matériau*, cliquez sur le bouton **Diffuse** et choisissez **Wc3Bitmap**.
4. Cliquez sur **Choisir un bitmap...** et choisissez une texture. Ou cliquez sur **Ouvrir l'explorateur de textures** et prenez-en une dans le jeu (cherchez `crate` ou `wood`).
5. Faites glisser le matériau sur la caisse.

![Le Wc3Material avec ses emplacements de textures.](mat_wc3material_r3.png)

!!! tip
    Vous avez déjà un matériau Standard ou Physical ? La *Correction des matériaux* le convertit pour vous en Wc3Material.

## Étape 3 : la collision

Le joueur doit pouvoir cliquer sur la caisse.

1. Sélectionnez la caisse.
2. Ouvrez **WhiteoutDex > [[t:menu_anim_general]]** et cliquez sur **Boîte de collision en un clic**.

## Étape 4 : une animation Stand

Chaque modèle a besoin d'au moins une séquence, même si rien ne bouge.

1. Ouvrez **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Cliquez sur **Nouveau...**, choisissez le préréglage *Stand* et cliquez sur **OK**.

## Étape 5 : exporter et tester

1. Exportez la caisse sous le nom `Crate.mdx` (voir la leçon *Exporter un modèle*).
2. Regardez le cadre de la fenêtre d'export : s'il est jaune, cliquez sur [[t:exp_fix_all_btn]].
3. Regardez le résultat dans l'aperçu : **WhiteoutDex > [[t:menu_renderer]]**.

Vous avez créé votre premier modèle Warcraft III.
