# Problèmes de la scène et Tout corriger

Avant l'export, WhiteoutDex vérifie votre scène. Il cherche ce qui casserait le modèle dans le jeu ou ferait échouer l'export. Vous voyez le résultat dans la fenêtre d'export.

[[shot:exporter_problems|La fenêtre d'export a trouvé trois problèmes.]]

| N° | Fonction |
|---|---|
| [[ctl:1092]] | Répare tous les problèmes en une fois. Cela modifie votre scène. Chaque type de réparation est une étape d'annulation. Vous pouvez donc l'annuler avec [[key:Ctrl+Z]]. |
| [[ctl:1093]] | Ouvre la liste de tous les problèmes. |

## La liste des problèmes

[[shot:scene_problems|Chaque ligne est un problème.]]

| N° | Fonction |
|---|---|
| [[ctl:2000]] | Les problèmes. Double-cliquez sur une ligne pour sélectionner l'objet dans la scène. |
| [[ctl:2004]] | Vérifie à nouveau la scène, par exemple après une réparation à la main. |
| [[ctl:2002]] | Répare uniquement les problèmes sélectionnés dans la liste. |
| [[ctl:2003]] | Répare tous les problèmes de la liste. |
| [[ctl:1]] | Ferme la liste. |

## Ce que WhiteoutDex répare

| Problème | Action de [[t:exp_fix_all_btn]] |
|---|---|
| Deux objets ont le même nom | Les renomme : `_2`, `_3`, etc. Le jeu a besoin de noms uniques. |
| Maillage vide | Le supprime. |
| Editable Poly | Ajoute un modificateur *Edit Mesh* pour que l'exportateur reçoive des triangles. |
| *Edit Mesh* au-dessus de *Skin* | Le réduit (collapse) pour que le skin reste correct. |
| Contrôleur que le jeu ne peut pas lire | Le remplace par un contrôleur linéaire. |
| Maillage avec un matériau Multi/Sub-Object | Divise le maillage pour que chaque partie ait un seul matériau. |
| Le matériau n'est pas un Wc3Material | Le convertit. Les réglages viennent de la *Correction des matériaux* (voir cette page). |
| Deux matériaux sont identiques | Les fusionne en un seul. |

!!! tip
    Vous n'êtes pas obligé de réparer les problèmes. L'exportateur fonctionne aussi sans réparation. Mais le modèle peut avoir un aspect incorrect dans le jeu.
