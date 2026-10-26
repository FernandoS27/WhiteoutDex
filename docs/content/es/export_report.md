# El informe de exportación

Después de una exportación, WhiteoutDex te dice solo lo que necesita tu atención. Cuando todo está bien, no ves nada y el archivo simplemente está ahí.

Cuando algo necesita tu atención, se abre una ventana **WhiteoutDex Export**:

- La primera línea cuenta los errores y las advertencias: *Export completed with 0 error(s) and 2 warning(s).*
- Cada línea siguiente nombra un objeto y el problema, por ejemplo `WARNING: [Cape] …`.
- Se muestran 50 líneas como máximo. La lista completa está en el archivo `mdlx_export_debug.log` (consulta *Archivos y ajustes*).

## Errores y advertencias

| Tipo | Significado | Qué haces |
|---|---|---|
| **ERROR** | Algo no se pudo exportar. El modelo puede estar roto. | Corrige el objeto nombrado y exporta otra vez. |
| **ADVERTENCIA** | El modelo funciona, pero algo puede verse distinto que en 3ds Max. | Lee la línea. A menudo la comprobación de la escena (*Corregir todo*) lo repara. |

!!! tip
    Usa antes [[t:exp_fix_all_btn]] en la ventana de exportación. Repara los problemas más comunes antes de exportar, así el informe es corto.

El importador tiene el mismo tipo de informe, con el título **WhiteoutDex Import**.
