# Tutorial: importar un modelo

En esta lección cargas un modelo de Warcraft III en 3ds Max y miras qué crea WhiteoutDex a partir de él.

**Necesitas:** un archivo de modelo. WhiteoutDex trae ejemplos en la carpeta `samples` del código fuente, por ejemplo `HeroPaladin.mdx`. También puedes usar cualquier `.mdx` o `.mdl` de internet o de tus propios mapas.

## Paso 1: inicia la importación

1. Elige **WhiteoutDex > [[t:menu_importer]]**.
2. Elige el archivo `HeroPaladin.mdx` y haz clic en **Open**.

Se abre la ventana de importación.

![La ventana de importación. Muestra lo que contiene el modelo.](importer.png)

## Paso 2: elige las opciones

1. En *Ajustes rápidos*, elige **Todo**.
2. En *Modo*, elige **Escena nueva**.
3. Haz clic en **Importar**.

Después de unos segundos, el modelo está en tu escena.

![El Paladin importado con sus puntos de anclaje (amarillo) y sus objetos de evento (verde).](tut_model_helpers.png)

## Paso 3: mira la escena

Ahora la escena contiene:

- **Las mallas** del modelo con *Wc3Materials* y las texturas.
- **Los huesos** que mueven las mallas.
- **Puntos de anclaje** con nombres amarillos, por ejemplo *Hand Right Ref*.
- **Objetos de evento** con nombres verdes, por ejemplo las huellas *FPT…* y los sonidos *SND…*.
- **Formas de colisión**, **emisores de partículas** y **estelas**, si el modelo los tiene.

## Paso 4: reproduce las animaciones

1. Abre **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Haz doble clic en una secuencia, por ejemplo *Walk*.

3ds Max reproduce la animación de caminar.

!!! tip
    ¿Demasiados ayudantes en la pantalla? Abre el [[t:menu_node_manager]], marca *Ayudantes / Dummies*, *Anclajes* y *Objetos de evento*, y haz clic en **Ocultar**.

**Próxima lección:** mira el modelo en el previsualizador WhiteoutFlakes.
