# Tutoriel : exporter un modèle

Vous enregistrez maintenant le modèle à nouveau comme fichier Warcraft III.

**Il vous faut :** la scène de la leçon *Importer un modèle*.

## Étape 1 : lancer l'export

1. Choisissez **WhiteoutDex > [[t:menu_exporter]]**.
2. Choisissez un dossier et un nom de fichier, par exemple `MyPaladin.mdx`. Cliquez sur **Enregistrer**.

La fenêtre d'export s'ouvre.

![La fenêtre d'export. Classic est sélectionné.](exporter.png)

## Étape 2 : choisir le jeu

- Pour le jeu classique, choisissez **Classic**. Les textures deviennent des `.blp`.
- Pour Reforged, choisissez **Reforged**. Les textures deviennent des `.dds`.

Le Paladin est un modèle classique. Choisissez donc **Classic**.

## Étape 3 : vérifier la scène

Regardez le cadre au-dessus des boutons :

- **Vert – Aucun problème :** continuez.
- **Jaune – problèmes :** cliquez sur [[t:exp_fix_all_btn]]. WhiteoutDex répare la scène. Voir *Problèmes de la scène et Tout corriger*.

## Étape 4 : exporter

1. Dans l'onglet *Textures*, vérifiez que *Convertir les textures à l'export* est activé.
2. Cliquez sur **Exporter**.

Le fichier `MyPaladin.mdx` et ses textures sont maintenant dans votre dossier. Si un point demande votre attention, le rapport d'export s'ouvre (voir *Le rapport d'export*).

## Étape 5 : tester dans le jeu

1. Copiez le modèle et ses textures dans le dossier de votre carte, ou importez-les avec le **Import Manager** du World Editor.
2. Dans le World Editor, donnez votre modèle à une unité dans l'*Object Editor* (champ *Art – Model File*).
3. Testez la carte.

!!! tip
    L'exportateur mémorise toutes les options. Le prochain export du même modèle ne demande qu'un clic.
