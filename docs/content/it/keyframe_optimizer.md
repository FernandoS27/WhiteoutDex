# Ottimizzatore di fotogrammi chiave

Le animazioni da altri programmi o dal motion capture hanno spesso una chiave su ogni fotogramma. Questo rende il modello grande. L'Ottimizzatore di fotogrammi chiave rimuove le chiavi che non servono, e il movimento resta quasi uguale.

Aprilo con **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Prima seleziona gli oggetti animati.

[[shot:keyframe_optimizer|L'Ottimizzatore di fotogrammi chiave.]]

## Impostazioni

| N. | Cosa fa |
|---|---|
| [[ctl:7#2]] | **Soglia:** quanto può cambiare la curva. Un valore più alto rimuove più chiavi. Inizia con 0.5. |
| [[ctl:11#2]] | **Passo:** quanto precisa è la misura dello strumento, in fotogrammi. Resta sotto 0.49. |
| [[ctl:13#2]] | Scrive i dettagli nel MAXScript Listener. Lascialo disattivo, perché è più lento. |

## Ottimizzare

| N. | Cosa fa |
|---|---|
| [[ctl:3]] | Ordina l'elenco per tempo o per nome. |
| [[ctl:5]] | Le sequenze del modello. Seleziona quelle che vuoi ottimizzare. |
| [[ctl:7]] | Ottimizza le chiavi di posizione. |
| [[ctl:8]] | Ottimizza le chiavi di rotazione. |
| [[ctl:9]] | Ottimizza le chiavi di scala. |
| [[ctl:14]] | Ottimizza le sequenze selezionate. |
| [[ctl:15]] | Ottimizza tutte le sequenze. |
| [[ctl:16]] | Elimina le chiavi che non sono dentro nessuna sequenza. Nel gioco non fanno niente. |

!!! tip
    Salva la scena prima di ottimizzare. Dopo controlla l'animazione. Se qualcosa appare sbagliato, annulla e prova una soglia più piccola.
