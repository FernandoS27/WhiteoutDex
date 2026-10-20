# Séquences globales

Une **séquence globale** est une animation qui tourne en boucle en permanence, quelle que soit l'animation lue par l'unité. Utilisez-la pour ce qui ne s'arrête jamais : un cristal qui tourne, une torche qui brûle, un drapeau dans le vent.

Chaque séquence globale a une durée en images. Les clés d'un objet peuvent appartenir à une séquence globale au lieu de la ligne de temps normale.

Ouvrez le gestionnaire de séquences globales avec le bouton [[t:seq_globals_btn]] du *gestionnaire de séquences*.

[[shot:global_sequences|Le gestionnaire de séquences globales avec les deux séquences globales du modèle d'exemple.]]

La liste du haut montre toutes les séquences globales : numéro, images, type et objets qui l'utilisent.

| N° | Fonction |
|---|---|
| [[ctl:7]] | L'image de fin (la durée) de la séquence globale sélectionnée. |
| [[ctl:8]] | Relit la scène. |
| [[ctl:9]] | Supprime la séquence globale sélectionnée. |
| [[ctl:15]] | La durée d'une nouvelle séquence globale, en images. |
| [[ctl:24]] | Crée une nouvelle séquence globale sur les objets sélectionnés. |
| [[ctl:25]] | Modifie la séquence globale sélectionnée. |

## Faire tourner un objet en permanence

1. Sélectionnez l'objet.
2. Réglez la **Durée**, par exemple 60 images.
3. Cliquez sur **Nouveau**. Le curseur temporel va à l'image 0. Les options du contrôleur, des canaux et de la rotation apparaissent.
4. Choisissez le **Contrôleur**, par exemple *Linear*, puis l'axe.
5. Placez le curseur temporel sur la dernière image d'un tour, par exemple 60.
6. Cliquez sur **> Horaire** (sens horaire) ou sur **Antihoraire <** (sens antihoraire).
7. Cliquez sur **Terminer**.

L'objet fait maintenant un tour en 60 images, dans toutes les animations.

Vous pouvez aussi animer à la main. Après **Nouveau**, posez des clés sur l'objet comme d'habitude, dans les limites de la durée. Cliquez ensuite sur **Terminer**. WhiteoutDex déplace ces clés dans la nouvelle séquence globale.

!!! note
    Le jeu compte les séquences globales en millisecondes. WhiteoutDex convertit les images pour vous.
