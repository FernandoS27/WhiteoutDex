# Problèmes et solutions

## Le menu WhiteoutDex est absent

- Vérifiez que votre version de 3ds Max est comprise entre 2016 et 2027.
- Réinstallez WhiteoutDex pendant que 3ds Max est fermé.
- 3ds Max 2026 et les versions plus récentes ont besoin de **.NET 8**. Installez-le depuis le site de Microsoft et relancez 3ds Max.

## Le modèle n'a pas de textures (il est blanc ou rose)

- Ouvrez [[t:menu_settings]] et vérifiez les dossiers du jeu. La ligne d'état doit indiquer que des archives ont été trouvées.
- Les modèles classiques cherchent dans les archives MPQ, les modèles Reforged dans CASC. Indiquez le dossier du jeu dont vient le modèle.
- Les textures des modèles personnalisés doivent se trouver à côté du fichier du modèle, ou dans un sous-dossier avec le chemin utilisé par le modèle (par exemple `Textures\`).

## Le modèle semble retourné dans le jeu

Les triangles sont orientés dans le mauvais sens. L'exportateur retourne les maillages qui sont orientés contre leurs propres normales. Si une partie semble encore fausse, sélectionnez-la et utilisez **Flip** sur ses normales dans 3ds Max.

## Le modèle est invisible ou clignote au bord de l'écran

Les **étendues** (la taille de chaque animation) sont trop petites. L'exportateur les mesure à chaque image. Si vous avez déplacé le modèle après l'import, exportez à nouveau. L'exportateur remplace les étendues qui ne contiennent pas le modèle.

## Des parties du modèle ne bougent pas avec les os

- Le maillage a besoin d'un modificateur **Skin** avec les os.
- Dans la fenêtre d'import, l'option *Skinné* doit être activée.
- Pour les modèles classiques, activez *Quantifier les poids de skin* dans la fenêtre d'export.

## L'export affiche des avertissements

Le rapport d'export liste ce qui demande votre attention. Chaque ligne nomme l'objet. La liste complète se trouve dans `mdlx_export_debug.log` (voir *Fichiers et paramètres*).

## 3ds Max affiche « Startup Failure Detection »

3ds Max ne s'est pas fermé correctement la dernière fois. Cliquez sur **Continue Without Restore**. Ne choisissez *Restore Factory Settings* que si vous voulez réinitialiser tout 3ds Max.

## Où trouver de l'aide ?

- **Discord :** le serveur WhiteoutDex (lien dans [[t:menu_about]]).
- **Hive Workshop :** le sujet WhiteoutDex.
- **GitHub :** signalez les bugs sur la page *Issues*. Joignez les fichiers journaux de *Fichiers et paramètres*.
