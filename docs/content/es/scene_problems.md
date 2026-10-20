# Problemas de la escena y Corregir todo

Antes de exportar, WhiteoutDex comprueba tu escena. Busca cosas que romperían el modelo en el juego o que harían fallar la exportación. Ves el resultado en la ventana de exportación.

[[shot:exporter_problems|La ventana de exportación encontró tres problemas.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1092]] | Repara todos los problemas a la vez. Esto cambia tu escena. Cada tipo de reparación es un paso de deshacer, así puedes revertirlo con [[key:Ctrl+Z]]. |
| [[ctl:1093]] | Abre la lista de todos los problemas. |

## La lista de problemas

[[shot:scene_problems|Cada fila es un problema.]]

| N.º | Qué hace |
|---|---|
| [[ctl:2000]] | Los problemas. Haz doble clic en una fila para seleccionar el objeto en la escena. |
| [[ctl:2004]] | Comprueba la escena otra vez, por ejemplo después de reparar algo a mano. |
| [[ctl:2002]] | Repara solo los problemas que seleccionaste en la lista. |
| [[ctl:2003]] | Repara todos los problemas de la lista. |
| [[ctl:1]] | Cierra la lista. |

## Qué repara WhiteoutDex

| Problema | Qué hace [[t:exp_fix_all_btn]] |
|---|---|
| Dos objetos tienen el mismo nombre | Les cambia el nombre: `_2`, `_3`, etc. El juego necesita nombres únicos. |
| Malla vacía | La borra. |
| Editable Poly | Añade un modificador *Edit Mesh*, así el exportador recibe triángulos. |
| *Edit Mesh* encima de *Skin* | Lo contrae, así el skin sigue siendo correcto. |
| Controlador que el juego no puede leer | Lo reemplaza por un controlador lineal. |
| Malla con un material Multi/Sub-Object | Divide la malla, así cada parte tiene un material. |
| El material no es un Wc3Material | Lo convierte. Los ajustes vienen de *Corrección de materiales* (consulta esa página). |
| Dos materiales son iguales | Los une en uno. |

!!! tip
    No tienes que reparar los problemas. El exportador también funciona sin repararlos. Pero el modelo puede verse mal en el juego.
