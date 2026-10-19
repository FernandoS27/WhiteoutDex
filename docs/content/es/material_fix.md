# Corrección de materiales

El exportador solo puede leer **Wc3Material**. Corrección de materiales convierte otros materiales (Standard, Physical, PBR) en Wc3Material. Conserva las texturas y define las opciones de Warcraft III que eliges aquí.

[[t:exp_fix_all_btn]] en la ventana de exportación usa los mismos ajustes.

Ábrela con **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|Corrección de materiales con los ajustes para modelos clásicos (SD).]]

Hay dos grupos de ajustes: uno para modelos clásicos (**SD**) y otro para Reforged (**HD**).

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | Ajustes para modelos clásicos. |
| [[ctl:5]] | Ajustes para modelos de Reforged. |

## Renderizado

| N.º | Qué hace |
|---|---|
| [[ctl:8]] | Cómo se dibuja el material. *Automático* hace transparentes los materiales con canal alfa. Consulta *Wc3Material* para ver todos los modos. |
| [[ctl:10]] | Se dibujan los dos lados de las caras. |
| [[ctl:11]] | El material ignora la luz. |
| [[ctl:12]] | El material ignora la niebla. |
| [[ctl:13]] | Usa un solo color para toda la malla. |
| [[ctl:14]] | Se dibuja encima de todo (sin prueba de profundidad). |
| [[ctl:15]] | No oculta lo que hay detrás. |
| [[ctl:16]] | Usa oclusión ambiental (solo Reforged). |
| [[ctl:17]] | Las caras traseras proyectan sombras (solo Reforged). |
| [[ctl:18]] | Toma *doble cara*, *sin sombreado* y *transparencia* del material antiguo en lugar de las casillas de arriba. |

## Texturas

| N.º | Qué hace |
|---|---|
| [[ctl:23]] | La carpeta que va delante de los nombres de las texturas en el modelo, por ejemplo `Textures\`. |
| [[ctl:26]] | Cómo se repiten las texturas: como en el material antiguo, en las dos direcciones, solo U, solo V o nada. |
| [[ctl:28]] | Un mapa normal o de relieve se convierte en el mapa *Normal* (Reforged). |
| [[ctl:29]] | Un mapa de emisión se convierte en el mapa *Emisiva* (Reforged). |

## Corregir todo y convertir ahora

| N.º | Qué hace |
|---|---|
| [[ctl:32]] | Qué ajustes usa [[t:exp_fix_all_btn]] en la ventana de exportación: SD o HD. |
| [[ctl:34]] | Añade `_wc3` al nombre de cada material convertido. |
| [[ctl:39]] | Convierte ahora los materiales de los objetos seleccionados. |
| [[ctl:40]] | Convierte ahora todos los materiales de la escena. |
| [[ctl:42]] | Devuelve esta pestaña a los valores predeterminados. |
| [[ctl:43]] | Cierra la ventana. Tus ajustes se guardan. |

!!! tip
    Guarda tu escena antes de convertir toda la escena.
