# Nombres de animación

Warcraft III encuentra una animación por su **nombre**. Una unidad que ataca reproduce la animación que empieza por *Attack*. Por eso los nombres de tus secuencias son importantes.

Un nombre tiene una **palabra principal** y puede tener **palabras extra**. Ejemplos: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Palabras principales

| Nombre | Cuándo la reproduce el juego |
|---|---|
| Stand | La unidad no hace nada. |
| Walk | La unidad se mueve. |
| Attack | La unidad ataca. |
| Spell | La unidad lanza un hechizo. |
| Death | La unidad muere. |
| Decay | El cuerpo muerto se descompone (*Decay Flesh* y después *Decay Bone*). |
| Birth | Se construye un edificio o aparece una unidad. |
| Dissipate | Una unidad invocada o espiritual desaparece al morir. |
| Portrait | La cara en la ventana de retrato. *Portrait Talk* se reproduce mientras la unidad habla. |
| Morph | La unidad cambia de forma. |
| Sleep | La unidad duerme. |
| Cinematic | Solo la usan las cinemáticas y los mapas. |

## Palabras extra

| Palabra | Significado |
|---|---|
| Ready | Lista para luchar, por ejemplo *Stand Ready*. |
| Slam | Un golpe fuerte, por ejemplo *Attack Slam* o *Spell Slam*. |
| Victory | Después de ganar una lucha. |
| Hit | La unidad recibe un golpe. |
| Channel | Un hechizo que dura un tiempo. |
| Throw | Lanza algo. |
| Fast | Versión más rápida, por ejemplo *Walk Fast*. |
| Alternate | La segunda forma de una unidad, por ejemplo después de *Metamorphosis*. |
| Upgrade First … Fifth | Un edificio después de su 1.ª a 5.ª mejora. |
| Work, Gold, Lumber | Animaciones de los trabajadores. |
| Swim | Se mueve en el agua. |
| Defend | Con *Defend* activado. |
| Talk | Habla (con *Portrait*). |

## Muchas animaciones con el mismo nombre

Puedes tener más de una animación con la misma palabra principal, por ejemplo `Stand - 1`, `Stand - 2` y `Stand - 3`. El juego elige una al azar. El valor de **rareza** en el [[t:menu_sequence_manager]] indica al juego con qué frecuencia elige una animación: un valor más alto significa menos veces.

## Duraciones que funcionan bien

El [[t:menu_sequence_manager]] tiene preajustes con estas duraciones (a 30 fotogramas por segundo):

| Preajuste | Fotogramas |
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
    *Death*, *Decay*, *Birth* y *Dissipate* se reproducen solo una vez. Márcalas como **Sin bucle** en el Gestor de secuencias.
