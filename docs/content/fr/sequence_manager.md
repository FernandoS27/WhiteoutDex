# Gestionnaire de séquences

Un modèle Warcraft III range toutes ses animations sur une seule ligne de temps. Chaque animation est une **séquence** : un nom, une image de début et une image de fin. Le gestionnaire de séquences crée et modifie ces séquences.

Ouvrez-le avec **WhiteoutDex > [[t:menu_sequence_manager]]** ou avec le bouton **Sequence** de la barre latérale.

[[shot:sequence_manager_selected|Le gestionnaire de séquences avec la séquence « Stand - 1 » sélectionnée.]]

## La liste

| N° | Fonction |
|---|---|
| [[ctl:4]] | Saisissez un mot pour n'afficher que les séquences dont le nom contient ce mot. |
| [[ctl:6]] | Vide le champ de recherche et affiche à nouveau toutes les séquences. |
| [[ctl:7]] | Toutes les séquences du modèle. Cliquez pour en sélectionner une. Maintenez [[key:Ctrl]] pour en sélectionner plusieurs. **Double-cliquez** sur une séquence pour la lire dans le viewport. Les lignes comme `[GLOBAL 1]` sont des séquences globales. |
| [[ctl:10]] | Sélectionne toutes les séquences. |
| [[ctl:11]] | Ne sélectionne aucune séquence. |
| [[ctl:12]] | Sélectionne les séquences non sélectionnées et désélectionne les autres. |

## Créer et supprimer

| N° | Fonction |
|---|---|
| [[ctl:14]] | Crée une nouvelle séquence (voir plus bas). |
| [[ctl:15]] | Supprime les séquences sélectionnées. Les clés d'animation restent sur la ligne de temps. |
| [[ctl:16]] | Copie les séquences sélectionnées. |
| [[ctl:17]] | Ouvre le *gestionnaire de séquences globales*. |

## Modifier la séquence sélectionnée

| N° | Fonction |
|---|---|
| [[ctl:20]] | Le nom. Le nom indique au jeu quand lire l'animation. Voir *Noms d'animation*. |
| [[ctl:23]] | Utilise l'image actuelle du curseur temporel comme image de début. |
| [[ctl:26]] | La première image de la séquence. |
| [[ctl:28]] | Utilise l'image actuelle comme image de fin. |
| [[ctl:31]] | La dernière image de la séquence. |
| [[ctl:32]] | **Rareté** (0–40). Pour des animations de même nom, une valeur plus élevée signifie que le jeu lit celle-ci moins souvent. |
| [[ctl:36]] | **Vitesse** (0–700). La vitesse de déplacement de l'unité quand elle lit cette animation de marche. La plupart des unités ont entre 250 et 400. |
| [[ctl:40]] | L'animation est lue une seule fois et ne se répète pas. Utilisez cette option pour *Death*, *Attack* et *Spell*. |
| [[ctl:41]] | L'exportateur laisse cette séquence de côté. |
| [[ctl:44]] | Avance l'image de début d'une image. Le bouton [[ctl:45]] la recule d'une image. |
| [[ctl:47]] | Avance l'image de fin d'une image. Le bouton [[ctl:48]] la recule d'une image. |

## Enregistrer et charger

| N° | Fonction |
|---|---|
| [[ctl:50]] | Charge des séquences depuis un fichier texte. |
| [[ctl:51]] | Enregistre les séquences dans un fichier texte. Utilisez cette fonction pour les copier dans une autre scène. |

## Créer une séquence

[[shot:sequence_new|La fenêtre pour une nouvelle séquence.]]

1. Cliquez sur [[t:seq_new_btn]].
2. Choisissez un préréglage, par exemple *Walk*. Le préréglage définit un nom et une durée adaptés.
3. Vérifiez le nom et les images. La nouvelle séquence commence après la dernière.
4. Cliquez sur **OK**.

| N° | Fonction |
|---|---|
| [[ctl:4]] | Des préréglages pour les animations courantes, ou *Custom*. |
| [[ctl:7]] | Le nom de la nouvelle séquence. |
| [[ctl:12]] | La première image. |
| [[ctl:16]] | La dernière image. |
| [[ctl:17]] | Active la valeur de rareté. |
| [[ctl:21]] | Active la vitesse de déplacement. |
| [[ctl:25]] | L'animation est lue une seule fois. |

!!! tip
    Laissez quelques images vides entre deux séquences. Ainsi, les clés d'une animation ne modifient pas la suivante.

!!! note
    WhiteoutDex enregistre les séquences dans votre fichier `.max`. Les anciennes scènes avec des séquences dans des *note tracks* fonctionnent toujours : WhiteoutDex les lit aussi.
