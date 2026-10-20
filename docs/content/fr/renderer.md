# Aperçu WhiteoutFlakes

**WhiteoutFlakes** montre votre modèle comme Warcraft III le montre : avec les shaders du jeu, la couleur d'équipe, l'éclairage, les particules et les traînées. Il suit 3ds Max en direct. Quand vous déplacez le curseur temporel ou modifiez quelque chose, l'aperçu se met à jour.

Ouvrez-le avec **WhiteoutDex > [[t:menu_renderer]]** ou avec le bouton **Renderer** de la barre latérale.

![WhiteoutFlakes à côté de 3ds Max 2027, avec un modèle Reforged en couleur d'équipe.](media_1.png)

## Barre d'outils

| Élément | Fonction |
|---|---|
| **Animation** | Lit une séquence du modèle. Ou bien *Free Range* pour suivre le curseur temporel de Max. |
| **Caméra** | *Caméra libre* ou l'une des caméras de la scène, par exemple la caméra de portrait. |
| **Équipe** | La couleur de joueur du modèle. |
| **Éclairage** | *En jeu* (comme le jeu), *Glue* (comme les menus) ou *Dynamique*. |
| **Sync Camera** | L'aperçu suit le viewport actif de 3ds Max. |
| **Resync** | Relit toute la scène, par exemple après l'ajout d'objets. |

## Menu

| Menu | Contenu |
|---|---|
| **Affichage** | Affiche ou masque la grille, les particules, les traînées et les objets d'événement. *Graphismes de Warcraft III* : les textures à utiliser (*Suivre le mode de rendu*, *Classic*, *Reforged (HD)*, *Definitive*). *Tileset* : le sol. |
| **Débogage** | Des marqueurs pour les formes de collision et les lumières, des vues spéciales (albédo, normales, niveau de détail, nombre de lumières …) et le choix du niveau de détail. |
| **Paramètres** | Arrière-plan, luminosité, volume du son, heure du jour, éclairage par image (IBL), ombres, brouillard, moteur graphique (Direct3D 11, Direct3D 12, Vulkan) et dossiers du jeu. |

## Bon à savoir

- Les particules et les traînées sont simulées comme dans le jeu. Quand vous sautez à un autre endroit du curseur temporel, l'aperçu calcule les effets jusqu'à la nouvelle image. Ils ont alors l'aspect qu'ils auraient si l'animation avait été lue jusque-là.
- L'aperçu enregistre ses réglages dans `WhiteoutFlakes.ini`, à côté de `3dsmax.exe`.
- Si l'aperçu n'affiche rien ou plante, regardez `WhiteoutDex_render.log` (voir *Fichiers et paramètres*).

!!! tip
    Choisissez *Classic* ou *Reforged (HD)* sous **Affichage > Graphismes de Warcraft III**. Vous voyez ainsi l'aspect de votre modèle dans chaque version du jeu.
