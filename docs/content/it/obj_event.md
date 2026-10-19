# Oggetto evento

Un oggetto evento avvia qualcosa in un momento preciso di un'animazione: un suono quando la spada colpisce, un'impronta quando il piede tocca il terreno, sangue quando l'unità muore, un modello che compare.

Crealo con **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Mettilo dove deve accadere l'evento e collegalo a un osso se si deve muovere.

Nella viewport ogni oggetto evento mostra il suo nome in verde.

[[shot:obj_event_r2|Il rollout Oggetto evento di un suono di morte del modello di esempio.]]

## Scegliere l'evento

| N. | Cosa fa |
|---|---|
| [[ctl:5]] | Il tipo di evento (vedi la tabella sotto). |
| [[ctl:8]] | L'evento preciso dall'elenco del gioco, per esempio un suono o un colore del sangue. |
| [[ctl:13]] | Usa la tua scelta e dà il nome all'oggetto. La riga sotto il pulsante mostra il codice dell'evento. |

Puoi anche scrivere tu un codice di evento in *Dati personalizzati*.

| Tipo | Il codice inizia con | Cosa succede |
|---|---|---|
| FootPrint | `FPT` | Un'impronta sul terreno. |
| BloodSplat | `SPL` | Una macchia sul terreno, per esempio sangue. |
| UberSplat | `UBR` | Un grande segno sul terreno, per esempio un cratere o la base di un edificio. |
| Sound | `SND` | Un suono. |
| SpawnObject | `SPN` | Compare un modello, per esempio una parte del corpo che cade. |

## Quando succede

| N. | Cosa fa |
|---|---|
| [[ctl:19]] | I momenti in cui succede l'evento. |
| [[ctl:20]] | Sposta il cursore del tempo sul fotogramma giusto, poi clicca qui per aggiungere un momento. |
| [[ctl:21]] | Rimuove il momento selezionato. |
| [[ctl:22]] | Rimuove tutti i momenti. |

!!! tip
    Usa il *Gestore sequenze* per andare all'animazione. Poi sposta il cursore un fotogramma alla volta per trovare il momento preciso, per esempio quando il piede tocca il terreno.
