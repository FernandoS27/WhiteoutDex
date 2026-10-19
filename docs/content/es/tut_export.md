# Tutorial: exportar un modelo

Ahora guardas otra vez el modelo como un archivo de Warcraft III.

**Necesitas:** la escena de la lección *Importar un modelo*.

## Paso 1: inicia la exportación

1. Elige **WhiteoutDex > [[t:menu_exporter]]**.
2. Elige una carpeta y un nombre de archivo, por ejemplo `MyPaladin.mdx`. Haz clic en **Save**.

Se abre la ventana de exportación.

![La ventana de exportación. Clásico está seleccionado.](exporter.png)

## Paso 2: elige el juego

- Para el juego clásico, elige **Clásico**. Las texturas se convierten en `.blp`.
- Para Reforged, elige **Reforged**. Las texturas se convierten en `.dds`.

El Paladin es un modelo clásico, así que elige **Clásico**.

## Paso 3: comprueba la escena

Mira el cuadro que está sobre los botones:

- **Verde – Sin problemas:** continúa.
- **Amarillo – problemas:** haz clic en [[t:exp_fix_all_btn]]. WhiteoutDex repara la escena. Consulta *Problemas de la escena y Corregir todo*.

## Paso 4: exporta

1. En la pestaña *Texturas*, comprueba que *Convertir las texturas al exportar* está activado.
2. Haz clic en **Exportar**.

El archivo `MyPaladin.mdx` y sus texturas están ahora en tu carpeta. Si algo necesita tu atención, se abre el informe de exportación (consulta *El informe de exportación*).

## Paso 5: pruébalo en el juego

1. Copia el modelo y sus texturas en la carpeta de tu mapa, o impórtalos con el **Import Manager** del World Editor.
2. En el World Editor, da tu modelo a una unidad en el *Object Editor* (campo *Art – Model File*).
3. Prueba el mapa.

!!! tip
    El exportador recuerda todas las opciones. La próxima exportación del mismo modelo necesita solo un clic.
