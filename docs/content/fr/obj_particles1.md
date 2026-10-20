# Émetteur de particules 1

Le Particle Emitter 1 n'émet pas des images, mais de **petits modèles**. Utilisez-le par exemple pour des feuilles qui tombent, des pierres ou des flèches qui sont elles-mêmes des modèles.

Créez-le avec **Create > Geometry > Particle Systems > Wc3Particles1**.

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration.]]

| N° | Fonction |
|---|---|
| [[ctl:2020]] | Charge tous les réglages depuis un fichier `.ini`. |
| [[ctl:2021]] | Enregistre tous les réglages dans un fichier `.ini`. |
| [[ctl:2022]] | Si cette option est activée, *Importer* charge aussi les clés animées des valeurs. |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options.]]

| N° | Fonction |
|---|---|
| [[ctl:5002]] | La vitesse à laquelle les modèles s'éloignent. |
| [[ctl:5004]] | L'angle vertical de la direction. |
| [[ctl:5006]] | L'angle latéral de la direction. |
| [[ctl:5008]] | Attire les modèles vers le bas. |
| [[ctl:2030]] | **Contrôle :** la façon dont une valeur animée change entre les clés. Chaque valeur a son propre contrôle. |

## Timing Options

[[shot:obj_particles1_r3|Timing Options.]]

| N° | Fonction |
|---|---|
| [[ctl:5010]] | La durée de vie d'un modèle, en secondes. |
| [[ctl:5012]] | Le nombre de modèles émis par seconde. |

## Model Options

[[shot:obj_particles1_r4|Model Options.]]

| N° | Fonction |
|---|---|
| [[ctl:5017]] | Le dossier du modèle dans le jeu. |
| [[ctl:5016]] | Le nom de fichier du modèle, par exemple `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Choisissez un modèle dans les archives du jeu. |
| [[ctl:2023]] | Choisissez un fichier de modèle sur votre disque. |
| [[ctl:5014]] | La taille des modèles dans l'aperçu du viewport. |

!!! note
    Les modèles classiques écrivent souvent l'extension `.mdl` dans ce chemin, même quand le fichier du jeu est un `.mdx`. Le jeu trouve les deux.
