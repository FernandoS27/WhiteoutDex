# L'exportateur

L'exportateur enregistre votre scène comme modèle Warcraft III (`.mdx` ou `.mdl`). Il peut aussi convertir vos textures aux formats du jeu.

## Ouvrir l'exportateur

- Le menu **WhiteoutDex > [[t:menu_exporter]]**
- Le bouton **Export** de la barre latérale WhiteoutDex
- **File > Export**, puis choisissez *Warcraft III MDX/MDL Model*

Choisissez un nom de fichier. La fenêtre d'export s'ouvre alors.

## La fenêtre d'export

[[shot:exporter|La fenêtre d'export avec l'onglet Géométrie.]]

Le haut de la fenêtre indique le nom du fichier. En dessous, vous voyez si le fichier est nouveau ou si un fichier de ce nom existe déjà.

| N° | Fonction |
|---|---|
| [[ctl:1001]] | **Nom du modèle.** Le nom enregistré dans le fichier. Le jeu ne l'affiche pas, mais d'autres outils l'affichent. |
| [[ctl:1011]] | Exporte pour le jeu classique (MDX version 800). Les textures deviennent des fichiers `.blp`. |
| [[ctl:1012]] | Exporte pour Reforged (MDX version 1800). Les textures deviennent des fichiers `.dds`. |
| [[ctl:1015]] | Affiche les options du maillage (cette image). |
| [[ctl:1016]] | Affiche les options des textures. |
| [[ctl:1021]] | Combine en un seul les maillages qui utilisent le même matériau. Le modèle devient plus petit et plus rapide. |
| [[ctl:1027]] | Rend les normales identiques là où deux maillages se touchent. Les jointures disparaissent. N'utilisez pas cette option si vous utilisez des groupes de lissage pour des arêtes vives. |
| [[ctl:1028]] | Exporte aussi les os et les aides que rien n'utilise. Normalement, l'exportateur les laisse de côté. |
| [[ctl:1032]] | Modifie les poids de skin pour qu'ils fonctionnent dans le jeu classique. Le jeu classique n'accepte que des poids simples. Seulement pour [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | Si le fichier existe déjà, l'exportateur ajoute `_1`, `_2`, etc. au nom. Votre ancien fichier reste en place. |
| [[ctl:1031]] | Ouvre le dossier du fichier après l'export. |
| [[ctl:1]] | Lance l'export. |
| [[ctl:2]] | Ferme la fenêtre sans exporter. |

Le cadre vert sous les options indique que WhiteoutDex n'a trouvé aucun problème dans votre scène. S'il trouve des problèmes, le cadre est jaune. Voir *Problèmes de la scène et Tout corriger*.

### Onglet Textures

[[shot:exporter_textures|L'onglet Textures, ici pour un export classique.]]

| N° | Fonction |
|---|---|
| [[ctl:1060]] | Convertit les textures de vos matériaux en `.blp` (Classic) ou en `.dds` (Reforged). Les enregistre à côté du modèle. |
| [[ctl:1071]] | **Classic uniquement.** [[t:exp_blp_paletted_item]] ou *JPEG*. Le JPEG a plus de couleurs. Les fichiers palettisés sont plus petits. Avec le JPEG, vous réglez aussi la qualité (1–100). |
| [[ctl:1075]] | Mélange un peu les couleurs. Cela masque les paliers nets entre les couleurs dans les textures palettisées. |
| [[ctl:1061]] | Crée les versions réduites de la texture que le jeu utilise pour les modèles éloignés. Laissez cette option activée. |
| [[ctl:1062]] | Remplace les fichiers de texture déjà présents. Si l'option est désactivée, l'exportateur garde les fichiers existants. |

Pour **Reforged**, vous choisissez à la place le format DDS : *BC3 (DXT5)* ou *BC7*. Le BC7 est plus beau. Le BC3 fonctionne sur les cartes graphiques plus anciennes.

## Classic ou Reforged ?

| | Classic | Reforged |
|---|---|---|
| Jeu | Warcraft III 1.x et les graphismes classiques de Reforged | Graphismes Reforged (HD) |
| Version MDX | 800 | 1800 |
| Textures | `.blp` | `.dds` |
| Skin | Poids simples (utilisez [[t:exp_quantize_skin_weights_chk]]) | Poids complets |

!!! tip
    L'exportateur mémorise toutes les options pour le prochain export.
