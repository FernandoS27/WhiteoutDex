# Correction des matériaux

L'exportateur ne peut lire que des **Wc3Material**. La correction des matériaux convertit les autres matériaux (Standard, Physical, PBR) en Wc3Material. Elle garde les textures et applique les options Warcraft III que vous choisissez ici.

[[t:exp_fix_all_btn]] utilise les mêmes réglages dans la fenêtre d'export.

Ouvrez-la avec **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|La correction des matériaux avec les réglages pour les modèles classiques (SD).]]

Il existe deux jeux de réglages : un pour les modèles classiques (**SD**) et un pour Reforged (**HD**).

| N° | Fonction |
|---|---|
| [[ctl:4]] | Les réglages pour les modèles classiques. |
| [[ctl:5]] | Les réglages pour les modèles Reforged. |

## Rendu

| N° | Fonction |
|---|---|
| [[ctl:8]] | La façon dont le matériau est dessiné. *Automatique* rend transparents les matériaux qui ont un canal alpha. Voir *Wc3Material* pour tous les modes. |
| [[ctl:10]] | Les deux côtés des faces sont dessinés. |
| [[ctl:11]] | Le matériau ignore la lumière. |
| [[ctl:12]] | Le matériau ignore le brouillard. |
| [[ctl:13]] | Utilise une seule couleur pour tout le maillage. |
| [[ctl:14]] | Dessiné par-dessus tout le reste (pas de test de profondeur). |
| [[ctl:15]] | Ne cache pas ce qui se trouve derrière. |
| [[ctl:16]] | Utilise l'occlusion ambiante (Reforged uniquement). |
| [[ctl:17]] | Les faces arrière projettent des ombres (Reforged uniquement). |
| [[ctl:18]] | Reprend *double face*, *non ombré* et *transparence* de l'ancien matériau au lieu des cases ci-dessus. |

## Textures

| N° | Fonction |
|---|---|
| [[ctl:23]] | Le dossier placé devant les noms de texture dans le modèle, par exemple `Textures\`. |
| [[ctl:26]] | La répétition des textures : comme dans l'ancien matériau, dans les deux sens, seulement en U, seulement en V, ou pas du tout. |
| [[ctl:28]] | Une normal map ou une bump map devient la map *Normal* (Reforged). |
| [[ctl:29]] | Une map d'émission devient la map *Emissive* (Reforged). |

## Tout corriger et convertir maintenant

| N° | Fonction |
|---|---|
| [[ctl:32]] | Les réglages utilisés par [[t:exp_fix_all_btn]] dans la fenêtre d'export : SD ou HD. |
| [[ctl:34]] | Ajoute `_wc3` au nom de chaque matériau converti. |
| [[ctl:39]] | Convertit maintenant les matériaux des objets sélectionnés. |
| [[ctl:40]] | Convertit maintenant tous les matériaux de la scène. |
| [[ctl:42]] | Remet cet onglet aux valeurs par défaut. |
| [[ctl:43]] | Ferme la fenêtre. Vos réglages sont enregistrés. |

!!! tip
    Enregistrez votre scène avant de convertir toute la scène.
