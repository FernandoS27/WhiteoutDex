# Tutoriel : points d'attache et événements

Dans le jeu, une unité a besoin d'emplacements pour les effets (points d'attache). Elle a aussi besoin de moments pour les sons et les empreintes de pas (objets d'événement). Dans cette leçon, vous ajoutez les deux à une unité.

**Il vous faut :** une unité avec des os et une animation *Walk*, par exemple le Paladin importé.

## Partie 1 : points d'attache

### La méthode rapide

Si vos os ont des noms habituels (*Head*, *Chest*, *Hand*, *Foot* …) :

1. Ouvrez **WhiteoutDex > [[t:menu_anim_general]]**.
2. Cliquez sur **Points d'attache en un clic**. WhiteoutDex crée les points et les lie aux os.

### À la main

1. Créez un **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Cliquez sur **Sélectionner un objet (absorber)**, puis cliquez sur l'os, par exemple la main droite. Le point s'y place et se lie à l'os.
3. Dans **Paramètres du point d'attache**, choisissez **Type** *Hand* et **Côté** *Right*. Le point s'appelle maintenant *Hand Right Ref*.

Le jeu affiche à cet endroit les effets de sort sur la main droite.

![Les paramètres d'un point d'attache.](obj_attachpoint_r3.png)

**Points que chaque unité devrait avoir :** *Origin*, *Head*, *Chest*, *Overhead*. Les armes utilisent *Hand Left/Right* ou *Weapon*.

## Partie 2 : empreintes de pas

1. Créez un **Wc3 Event Object** et placez-le au pied gauche. Liez-le à l'os du pied.
2. Dans **Type d'événement**, choisissez le type *FootPrint*. Dans **Données**, choisissez une empreinte, par exemple une petite.
3. Cliquez sur **Confirmer**.
4. Ouvrez la séquence *Walk* dans le gestionnaire de séquences. Placez le curseur temporel sur l'image où le pied gauche touche le sol.
5. Cliquez sur **Ajouter**. Répétez pour chaque pas du pied gauche.
6. Faites de même pour le pied droit avec un deuxième objet d'événement.

## Partie 3 : un son

1. Créez un autre objet d'événement près de l'arme.
2. Choisissez le type *Sound* et un son d'impact dans **Données**. Cliquez ensuite sur **Confirmer**.
3. Allez à l'image de l'animation *Attack* où l'arme frappe. Cliquez sur **Ajouter**.

## Vérifier

Ouvrez l'aperçu. Avec **Affichage > Objets d'événement** activé, il montre les empreintes et joue les sons.

!!! tip
    Regardez un modèle du jeu (*Importer depuis le jeu*) : reprenez la façon dont Blizzard place les points d'attache et les événements.
