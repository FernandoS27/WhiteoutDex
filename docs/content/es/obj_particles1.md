# Emisor de partículas 1

El emisor de partículas 1 no emite imágenes, sino **modelos pequeños**. Úsalo para cosas como hojas que caen, piedras o flechas que son modelos en sí mismas.

Créalo con **Create > Geometry > Particle Systems > Wc3Particles1**.

## Configuración de partículas (*Particle Configuration*)

[[shot:obj_particles1_r1|La persiana Particle Configuration.]]

| N.º | Qué hace |
|---|---|
| [[ctl:2020]] | Carga todos los ajustes desde un archivo `.ini`. |
| [[ctl:2021]] | Guarda todos los ajustes en un archivo `.ini`. |
| [[ctl:2022]] | Si está activado, *Importar* carga también las claves animadas de los valores. |

## Opciones del emisor (*Emitter Options*)

[[shot:obj_particles1_r2|La persiana Emitter Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:5002]] | Con qué rapidez salen volando los modelos. |
| [[ctl:5004]] | Ángulo de la dirección hacia arriba y hacia abajo. |
| [[ctl:5006]] | Ángulo lateral de la dirección. |
| [[ctl:5008]] | Tira de los modelos hacia abajo. |
| [[ctl:2030]] | **Control:** cómo cambia un valor animado entre las claves. Cada valor tiene su propio control. |

## Opciones de tiempo (*Timing Options*)

[[shot:obj_particles1_r3|La persiana Timing Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:5010]] | Cuántos segundos vive un modelo. |
| [[ctl:5012]] | Cuántos modelos salen por segundo. |

## Opciones del modelo (*Model Options*)

[[shot:obj_particles1_r4|La persiana Model Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:5017]] | La carpeta del modelo dentro del juego. |
| [[ctl:5016]] | El nombre del archivo del modelo, por ejemplo `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Elige un modelo de los archivos del juego. |
| [[ctl:2023]] | Elige un archivo de modelo de tu disco. |
| [[ctl:5014]] | Tamaño de los modelos en la vista previa del visor. |

!!! note
    Los modelos clásicos suelen escribir la extensión `.mdl` en esta ruta, aunque el archivo del juego sea un `.mdx`. El juego encuentra los dos.
