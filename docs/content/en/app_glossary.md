# Glossary

Words you meet in WhiteoutDex and in this manual.

| Word | Meaning |
|---|---|
| **MDX / MDL** | The model files of Warcraft III. `.mdx` is binary and small. `.mdl` is text that you can read and edit. Both hold the same data. |
| **Classic** | Warcraft III version 1.x and the classic graphics. Models use MDX version 800. |
| **Reforged** | Warcraft III: Reforged with HD graphics. Models use MDX version 900 to 1800. WhiteoutDex exports version 1800. |
| **MPQ** | The archive files of the classic game (for example `War3.mpq`). |
| **CASC** | The storage of Warcraft III: Reforged. |
| **BLP** | The texture format of the classic game. |
| **DDS** | The texture format of Reforged. |
| **Geoset** | One mesh of a model. A model can have many geosets. |
| **Bone** | A node that moves vertices of a skinned mesh. |
| **Helper** | A node that moves other nodes, but you do not see it in the game. |
| **Skin** | Tells which bone moves which vertex, and how strong (weight). |
| **Sequence** | One animation of the model, for example *Walk*. It has a name, a start frame and an end frame. |
| **Global sequence** | An animation that loops all the time, independent of the current sequence. Good for things that always move, like a spinning crystal. |
| **Key / Keyframe** | A value at one point in time. The game moves between keys. |
| **Controller** | How the game moves between keys: *None* (jump), *Linear*, *Bezier* or *Hermite* (smooth). |
| **Attachment point** | A place on the model where the game attaches things: weapons, spell effects, the selection circle. |
| **Event object** | Starts something at one moment of an animation: a sound, a footprint, blood, a spawned model. |
| **Particle Emitter 1** | Emits small models. |
| **Particle Emitter 2** | Emits textured particles: fire, smoke, sparks. |
| **Ribbon emitter** | Draws a trail behind a moving point. |
| **Popcorn FX** | The particle system of Reforged (`.pkfx` files). |
| **Collision shape** | A box or sphere. The game uses it to know where you can click the model. |
| **Extents** | The size of the model in one sequence. The game uses it to know when the model is on the screen. |
| **Replaceable texture** | A texture the game fills in, for example the team color. |
| **Team color / team glow** | The player color on a unit. |
| **Filter mode** | How a material is drawn: solid, transparent, additive (glows) and more. |
| **Billboard** | A node that always turns to the camera. |
| **Art tier** | Which graphics the previewer shows: Classic, Reforged (HD) or Definitive. |
| **WhiteoutFlakes** | The previewer that shows your model like the game. |
