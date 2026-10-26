# Installation

Un seul programme d'installation ajoute WhiteoutDex à toutes les versions de 3ds Max de votre ordinateur (2016 à 2027).

## Installer

1. Fermez 3ds Max.
2. Téléchargez le fichier `WhiteoutDex_Setup_v<version>.exe` le plus récent. Il se trouve sur la page **Releases** de GitHub et sur Hive Workshop.
3. Lancez le programme d'installation. Windows demande les droits d'administrateur. Cliquez sur **Oui**.
4. Choisissez votre langue et suivez les étapes. Le programme d'installation trouve seul toutes vos versions de 3ds Max.
5. Lancez 3ds Max. Vous voyez maintenant le menu **WhiteoutDex** et la barre latérale WhiteoutDex.

!!! note
    3ds Max 2026 et les versions plus récentes ont besoin de **.NET 8**. Le programme d'installation vous prévient s'il manque.

## Où vont les fichiers

WhiteoutDex s'installe dans un seul dossier :

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

3ds Max charge tout depuis ce dossier. Vous n'avez aucun fichier à copier vous-même.

Vos réglages personnels (langue, dossiers du jeu, dernières options d'export) se trouvent dans le dossier de réglages de 3ds Max `plugcfg`. Ils restent en place quand vous mettez WhiteoutDex à jour.

## Mettre à jour

WhiteoutDex peut chercher les nouvelles versions au démarrage de 3ds Max (3ds Max 2018 et plus récent). Quand une nouvelle version est disponible, il vous pose la question :

- Cliquez sur **Oui** pour la télécharger et l'installer. 3ds Max se ferme pour la mise à jour.
- Cliquez sur **Non** pour continuer à travailler. La question revient le lendemain.

Vous pouvez désactiver cette recherche ou lancer une recherche immédiate dans [[t:menu_settings]].

## Désinstaller

Ouvrez **Paramètres Windows > Applications**, trouvez **WhiteoutDex** et cliquez sur **Désinstaller**.
