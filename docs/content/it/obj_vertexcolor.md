# Wc3 Vertex Color

Il modificatore **Wc3 Vertex Color** colora un'intera mesh. Puoi animare il colore, per esempio per far pulsare un cristallo o per far brillare di rosso una parte quando l'unità viene colpita. L'esportatore lo scrive come colore della *geoset animation* della mesh.

Aggiungilo dalla lista dei modificatori: seleziona una mesh e scegli **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Parametri di base del modificatore.]]

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | Il colore con cui si tinge la mesh. Il bianco lascia la texture com'è. Animalo con **Auto Key**. |
| [[ctl:7]] | Imposta il flag *Ombra esterna* della mesh. |
| [[ctl:9]] | Usa il colore. Quando è disattivo, viene scritto solo il flag. |

!!! tip
    Per far comparire e scomparire una parte, usa invece le chiavi di visibilità (*Generatore chiavi di visibilità*) o l'*Opacità* del Wc3Material.
