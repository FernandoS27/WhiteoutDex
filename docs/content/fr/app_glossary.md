# Glossaire

Les termes que vous rencontrez dans WhiteoutDex et dans ce manuel.

| Terme | Signification |
|---|---|
| **MDX / MDL** | Les fichiers de modèle de Warcraft III. `.mdx` est binaire et compact. `.mdl` est du texte que vous pouvez lire et modifier. Les deux contiennent les mêmes données. |
| **Classic** | Warcraft III version 1.x et les graphismes classiques. Les modèles utilisent MDX version 800. |
| **Reforged** | Warcraft III: Reforged avec les graphismes HD. Les modèles utilisent MDX version 900 à 1800. WhiteoutDex exporte la version 1800. |
| **MPQ** | Les fichiers d'archive du jeu classique (par exemple `War3.mpq`). |
| **CASC** | Le stockage de Warcraft III: Reforged. |
| **BLP** | Le format de texture du jeu classique. |
| **DDS** | Le format de texture de Reforged. |
| **Geoset** | Un maillage d'un modèle. Un modèle peut avoir de nombreux geosets. |
| **Os** | Un nœud qui déplace les sommets d'un maillage skinné. |
| **Aide** | Un nœud qui déplace d'autres nœuds, mais que vous ne voyez pas dans le jeu. |
| **Skin** | Indique quel os déplace quel sommet, et avec quelle force (poids). |
| **Séquence** | Une animation du modèle, par exemple *Walk*. Elle a un nom, une image de début et une image de fin. |
| **Séquence globale** | Une animation qui tourne en boucle en permanence, indépendamment de la séquence en cours. Idéale pour ce qui bouge toujours, comme un cristal qui tourne. |
| **Clé / clé d'animation** | Une valeur à un instant donné. Le jeu passe d'une clé à l'autre. |
| **Contrôleur** | La façon dont le jeu passe d'une clé à l'autre : *None* (saut), *Linear*, *Bezier* ou *Hermite* (doux). |
| **Point d'attache** | Un endroit du modèle où le jeu attache des éléments : armes, effets de sort, cercle de sélection. |
| **Objet d'événement** | Déclenche quelque chose à un moment d'une animation : un son, une empreinte de pas, du sang, un modèle qui apparaît. |
| **Particle Emitter 1** | Émet de petits modèles. |
| **Particle Emitter 2** | Émet des particules texturées : feu, fumée, étincelles. |
| **Émetteur de traînée** | Dessine une traînée derrière un point en mouvement (ribbon). |
| **Popcorn FX** | Le système de particules de Reforged (fichiers `.pkfx`). |
| **Forme de collision** | Une boîte ou une sphère. Le jeu s'en sert pour savoir où vous pouvez cliquer sur le modèle. |
| **Étendues** | La taille du modèle dans une séquence. Le jeu s'en sert pour savoir quand le modèle est à l'écran. |
| **Texture remplaçable** | Une texture que le jeu remplit, par exemple la couleur d'équipe. |
| **Couleur d'équipe / halo d'équipe** | La couleur du joueur sur une unité. |
| **Mode de filtrage** | La façon dont un matériau est dessiné : opaque, transparent, additif (brille), etc. |
| **Billboard** | Un nœud qui se tourne toujours vers la caméra. |
| **Graphismes de la scène** | Les graphismes que l'aperçu affiche : Classic, Reforged (HD) ou Definitive. |
| **WhiteoutFlakes** | L'aperçu qui montre votre modèle comme le jeu. |
