# Tutoriel : importer un modèle

Dans cette leçon, vous chargez un modèle Warcraft III dans 3ds Max. Puis vous regardez ce que WhiteoutDex en fait.

**Il vous faut :** un fichier de modèle. WhiteoutDex fournit des exemples dans le dossier `samples` du code source, par exemple `HeroPaladin.mdx`. Vous pouvez aussi prendre n'importe quel `.mdx` ou `.mdl` trouvé sur Internet ou dans vos propres cartes.

## Étape 1 : lancer l'import

1. Choisissez **WhiteoutDex > [[t:menu_importer]]**.
2. Choisissez le fichier `HeroPaladin.mdx` et cliquez sur **Ouvrir**.

La fenêtre d'import s'ouvre.

![La fenêtre d'import. Elle montre le contenu du modèle.](importer.png)

## Étape 2 : choisir les options

1. Sous *Réglages rapides*, choisissez **Tout**.
2. Sous *Mode*, choisissez **Nouvelle scène**.
3. Cliquez sur **Importer**.

Après quelques secondes, le modèle est dans votre scène.

![Le Paladin importé avec ses points d'attache (en jaune) et ses objets d'événement (en vert).](tut_model_helpers.png)

## Étape 3 : explorer la scène

La scène contient maintenant :

- **Les maillages** du modèle, avec des *Wc3Material* et les textures.
- **Les os** qui déplacent les maillages.
- **Les points d'attache**, avec des noms en jaune, par exemple *Hand Right Ref*.
- **Les objets d'événement**, avec des noms en vert, par exemple les empreintes de pas *FPT…* et les sons *SND…*.
- **Les formes de collision**, les **émetteurs de particules** et les **traînées**, si le modèle en a.

## Étape 4 : lire les animations

1. Ouvrez **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Double-cliquez sur une séquence, par exemple *Walk*.

3ds Max lit l'animation de marche.

!!! tip
    Trop d'aides à l'écran ? Ouvrez le [[t:menu_node_manager]], cochez *Aides / Dummies*, *Points d'attache* et *Objets d'événement*, puis cliquez sur **Masquer**.

**Leçon suivante :** regarder le modèle dans l'aperçu WhiteoutFlakes.
