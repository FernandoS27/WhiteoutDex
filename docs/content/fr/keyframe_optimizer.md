# Optimiseur de clés d'animation

Les animations venant d'autres programmes ou de la capture de mouvement ont souvent une clé à chaque image. Le modèle devient alors lourd. L'optimiseur de clés d'animation supprime les clés inutiles. Le mouvement reste presque identique.

Ouvrez-le avec **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Sélectionnez d'abord les objets animés.

[[shot:keyframe_optimizer|L'optimiseur de clés d'animation.]]

## Réglages

| N° | Fonction |
|---|---|
| [[ctl:7#2]] | **Seuil :** de combien la courbe peut changer. Une valeur plus élevée supprime plus de clés. Commencez avec 0.5. |
| [[ctl:11#2]] | **Pas :** la précision de la mesure, en images. Restez sous 0.49. |
| [[ctl:13#2]] | Écrit des détails dans le MAXScript Listener. Laissez cette option désactivée : elle ralentit l'outil. |

## Optimiser

| N° | Fonction |
|---|---|
| [[ctl:3]] | Trie la liste par temps ou par nom. |
| [[ctl:5]] | Les séquences du modèle. Sélectionnez celles à optimiser. |
| [[ctl:7]] | Optimise les clés de position. |
| [[ctl:8]] | Optimise les clés de rotation. |
| [[ctl:9]] | Optimise les clés d'échelle. |
| [[ctl:14]] | Optimise les séquences sélectionnées. |
| [[ctl:15]] | Optimise toutes les séquences. |
| [[ctl:16]] | Supprime les clés qui ne sont dans aucune séquence. Elles ne font rien dans le jeu. |

!!! tip
    Enregistrez votre scène avant l'optimisation. Vérifiez ensuite l'animation. Si quelque chose semble faux, annulez et essayez un seuil plus petit.
