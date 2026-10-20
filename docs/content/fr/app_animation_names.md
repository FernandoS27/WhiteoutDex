# Noms d'animation

Warcraft III trouve une animation grâce à son **nom**. Une unité qui attaque lit l'animation dont le nom commence par *Attack*. Les noms de vos séquences sont donc importants.

Un nom a un **mot principal** et peut avoir des **mots supplémentaires**. Exemples : `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Mots principaux

| Nom | Quand le jeu la lit |
|---|---|
| Stand | L'unité ne fait rien. |
| Walk | L'unité se déplace. |
| Attack | L'unité attaque. |
| Spell | L'unité lance un sort. |
| Death | L'unité meurt. |
| Decay | Le cadavre se décompose (*Decay Flesh*, puis *Decay Bone*). |
| Birth | Un bâtiment est construit ou une unité apparaît. |
| Dissipate | Une unité invoquée ou un esprit disparaît en fondu à sa mort. |
| Portrait | Le visage dans la fenêtre de portrait. *Portrait Talk* est lue pendant que l'unité parle. |
| Morph | L'unité change de forme. |
| Sleep | L'unité dort. |
| Cinematic | Utilisée seulement par les cinématiques et les cartes. |

## Mots supplémentaires

| Mot | Signification |
|---|---|
| Ready | Prêt au combat, par exemple *Stand Ready*. |
| Slam | Un coup puissant, par exemple *Attack Slam* ou *Spell Slam*. |
| Victory | Après un combat gagné. |
| Hit | L'unité est touchée. |
| Channel | Un sort qui dure un certain temps. |
| Throw | Lance quelque chose. |
| Fast | Version plus rapide, par exemple *Walk Fast*. |
| Alternate | La deuxième forme d'une unité, par exemple après *Metamorphosis*. |
| Upgrade First … Fifth | Un bâtiment après sa 1re à 5e amélioration. |
| Work, Gold, Lumber | Les animations des ouvriers. |
| Swim | Se déplace dans l'eau. |
| Defend | Avec *Defend* activé. |
| Talk | Parle (avec *Portrait*). |

## Plusieurs animations de même nom

Vous pouvez avoir plusieurs animations avec le même mot principal, par exemple `Stand - 1`, `Stand - 2` et `Stand - 3`. Le jeu en choisit une au hasard. La valeur de **rareté** dans le [[t:menu_sequence_manager]] indique au jeu à quelle fréquence il choisit une animation : une valeur plus élevée signifie moins souvent.

## Durées qui fonctionnent bien

Le [[t:menu_sequence_manager]] a des préréglages avec ces durées (à 30 images par seconde) :

| Préréglage | Images |
|---|---|
| Stand | 80 |
| Walk | 28 |
| Stand Ready | 20 |
| Attack | 24 |
| Attack Slam | 30 |
| Spell | 28 |
| Spell Slam | 24 |
| Death | 64 |
| Portrait Talk | 70 |

!!! note
    *Death*, *Decay*, *Birth* et *Dissipate* ne sont lues qu'une fois. Réglez-les sur **Sans boucle** dans le gestionnaire de séquences.
