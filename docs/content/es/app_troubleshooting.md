# Problemas y soluciones

## Falta el menú WhiteoutDex

- Comprueba que tu versión de 3ds Max está entre 2016 y 2027.
- Instala WhiteoutDex otra vez con 3ds Max cerrado.
- 3ds Max 2026 y versiones posteriores necesitan **.NET 8**. Instálalo desde Microsoft y vuelve a iniciar 3ds Max.

## El modelo no tiene texturas (es blanco o rosa)

- Abre [[t:menu_settings]] y comprueba las carpetas del juego. La línea de estado debe decir que se encontraron archivos.
- Los modelos clásicos buscan en los archivos MPQ, los modelos de Reforged en CASC. Define la carpeta del juego del que viene el modelo.
- Las texturas de los modelos propios deben estar junto al archivo del modelo o en una subcarpeta con la ruta que usa el modelo (por ejemplo `Textures\`).

## El modelo se ve del revés en el juego

Los triángulos miran hacia el lado equivocado. El exportador da la vuelta a las mallas que miran en contra de sus propias normales. Si una parte aún se ve mal, selecciónala y usa **Flip** en sus normales en 3ds Max.

## El modelo es invisible o parpadea en el borde de la pantalla

Las **extensiones** (el tamaño de cada animación) son demasiado pequeñas. El exportador las mide en cada fotograma. Si moviste el modelo después de importarlo, exporta otra vez. El exportador reemplaza las extensiones que no contienen el modelo.

## Partes del modelo no se mueven con los huesos

- La malla necesita un modificador **Skin** con los huesos.
- En la ventana de importación, la opción *Con skin* debe estar activada.
- Para modelos clásicos, activa *Cuantizar pesos de skin* en la ventana de exportación.

## La exportación muestra advertencias

El informe de exportación lista lo que necesita tu atención. Cada línea nombra el objeto. La lista completa está en `mdlx_export_debug.log` (consulta *Archivos y ajustes*).

## 3ds Max muestra "Startup Failure Detection"

3ds Max no se cerró bien la última vez. Haz clic en **Continue Without Restore**. No elijas *Restore Factory Settings*, salvo que quieras restablecer todo 3ds Max.

## ¿Dónde consigo ayuda?

- **Discord:** el servidor de WhiteoutDex (enlace en [[t:menu_about]]).
- **Hive Workshop:** el hilo de WhiteoutDex.
- **GitHub:** informa de errores en la página *Issues*. Añade los archivos de registro de *Archivos y ajustes*.
