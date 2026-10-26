# Instalación

Un solo instalador pone WhiteoutDex en todas las versiones de 3ds Max de tu ordenador (de 2016 a 2027).

## Instalar

1. Cierra 3ds Max.
2. Descarga el `WhiteoutDex_Setup_v<version>.exe` más reciente. Lo encuentras en la página **Releases** de GitHub y en Hive Workshop.
3. Inicia el instalador. Windows pide derechos de administrador. Haz clic en **Sí**.
4. Elige tu idioma y sigue los pasos. El instalador encuentra solo todas tus versiones de 3ds Max.
5. Inicia 3ds Max. Ahora ves el menú **WhiteoutDex** y la barra lateral de WhiteoutDex.

!!! note
    3ds Max 2026 y versiones posteriores necesitan **.NET 8**. El instalador te avisa si falta.

## Dónde van los archivos

WhiteoutDex se instala en una sola carpeta:

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

3ds Max carga todo desde allí. No necesitas copiar ningún archivo tú mismo.

Tus ajustes personales (idioma, carpetas del juego, últimas opciones de exportación) están en la carpeta de ajustes de 3ds Max `plugcfg`. Se conservan cuando actualizas WhiteoutDex.

## Actualizar

WhiteoutDex puede buscar versiones nuevas cuando se inicia 3ds Max (3ds Max 2018 y posteriores). Cuando hay una versión nueva, te pregunta:

- Haz clic en **Sí** para descargarla e instalarla. 3ds Max se cierra para la actualización.
- Haz clic en **No** para seguir trabajando. Te pregunta otra vez al día siguiente.

Puedes desactivar esto o buscar ahora en [[t:menu_settings]].

## Desinstalar

Abre **Configuración de Windows > Aplicaciones**, busca **WhiteoutDex** y haz clic en **Desinstalar**.
