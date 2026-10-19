# Animation names

Warcraft III finds an animation by its **name**. A unit that attacks plays the animation that starts with *Attack*. So the names of your sequences matter.

A name has a **main word** and can have **extra words**. Examples: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Main words

| Name | When the game plays it |
|---|---|
| Stand | The unit does nothing. |
| Walk | The unit moves. |
| Attack | The unit attacks. |
| Spell | The unit casts a spell. |
| Death | The unit dies. |
| Decay | The dead body rots away (*Decay Flesh*, then *Decay Bone*). |
| Birth | A building is built or a unit appears. |
| Dissipate | A summoned or spirit unit fades away when it dies. |
| Portrait | The face in the portrait window. *Portrait Talk* plays while the unit speaks. |
| Morph | The unit changes its form. |
| Sleep | The unit sleeps. |
| Cinematic | Used only by cinematics and maps. |

## Extra words

| Word | Meaning |
|---|---|
| Ready | Ready to fight, for example *Stand Ready*. |
| Slam | A strong hit, for example *Attack Slam* or *Spell Slam*. |
| Victory | After a won fight. |
| Hit | The unit is hit. |
| Channel | A spell that takes some time. |
| Throw | Throws something. |
| Fast | Faster version, for example *Walk Fast*. |
| Alternate | The second form of a unit, for example after *Metamorphosis*. |
| Upgrade First … Fifth | A building after its 1st to 5th upgrade. |
| Work, Gold, Lumber | Worker animations. |
| Swim | Moves in water. |
| Defend | With *Defend* turned on. |
| Talk | Speaks (with *Portrait*). |

## Many animations with the same name

You can have more than one animation with the same main word, for example `Stand - 1`, `Stand - 2` and `Stand - 3`. The game picks one by chance. The **rarity** value in the [[t:menu_sequence_manager]] tells the game how often it picks an animation: a higher value means less often.

## Lengths that work well

The [[t:menu_sequence_manager]] has presets with these lengths (at 30 frames per second):

| Preset | Frames |
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
    *Death*, *Decay*, *Birth* and *Dissipate* play only once. Set them to **Non Looping** in the Sequence Manager.
