# Tutoriel : un effet de feu

Dans cette leçon, vous placez un feu sur une torche avec un **Particle Emitter 2**.

## Étape 1 : créer l'émetteur

1. Choisissez *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Cliquez dans le viewport pour le placer. Déplacez-le en haut de la torche.
3. Liez-le à la torche (ou à l'os qui tient la torche) pour qu'il bouge avec elle.

## Étape 2 : la texture

1. Ouvrez le panneau déroulant **Texture Options**.
2. Cliquez sur **Parcourir MPQ / CASC** et choisissez une texture de feu du jeu (cherchez `fire` ou `flare`).
3. Réglez **Mode de fusion de la texture** sur **Additive**. Les particules additives brillent.

## Étape 3 : le mouvement

Dans **Emitter Options** et **Timing Options** :

1. **Vitesse :** une petite valeur, pour que les flammes montent lentement.
2. **Angle du cône :** petit, pour que les flammes restent groupées.
3. **Durée de vie :** environ 1 seconde.
4. **Taux d'émission :** environ 20 particules par seconde.

Vous voyez les particules tout de suite dans le viewport. Modifiez les valeurs jusqu'à ce que le feu vous plaise.

## Étape 4 : couleur et fondu

Dans **Particle Options** :

1. **Couleur :** début jaune, milieu orange, fin rouge foncé.
2. **Alpha :** début 0, milieu 255, fin 0. Les flammes apparaissent puis disparaissent en fondu.
3. **Échelle :** début petit, milieu plus grand, fin petit.

![Les Particle Options d'un émetteur.](obj_particles2_r6.png)

## Étape 5 : vérifier

Ouvrez l'aperçu (**WhiteoutDex > [[t:menu_renderer]]**). Il montre le feu comme le jeu le montrera.

!!! tip
    Enregistrez un feu qui vous plaît avec **Exporter** dans *Particle Configuration*. Chargez-le sur d'autres modèles avec **Importer**.
