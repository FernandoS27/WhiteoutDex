# Gestore sequenze

Un modello di Warcraft III tiene tutte le sue animazioni su una sola timeline. Ogni animazione è una **sequenza**: un nome, un fotogramma iniziale e un fotogramma finale. Il Gestore sequenze crea e modifica queste sequenze.

Aprilo con **WhiteoutDex > [[t:menu_sequence_manager]]** o con il pulsante **Gestore sequenze** nella barra laterale.

[[shot:sequence_manager_selected|Il Gestore sequenze con la sequenza "Stand - 1" selezionata.]]

## L'elenco

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | Scrivi una parola per mostrare solo le sequenze che hanno questa parola nel nome. |
| [[ctl:6]] | Svuota il campo di ricerca e mostra di nuovo tutte le sequenze. |
| [[ctl:7]] | Tutte le sequenze del modello. Clicca per selezionarne una. Tieni premuto [[key:Ctrl]] per selezionarne di più. **Fai doppio clic** su una sequenza per riprodurla nella viewport. Le righe come `[GLOBAL 1]` sono sequenze globali. |
| [[ctl:10]] | Seleziona tutte le sequenze. |
| [[ctl:11]] | Non seleziona nessuna sequenza. |
| [[ctl:12]] | Seleziona quelle non selezionate e deseleziona le altre. |

## Creare e rimuovere

| N. | Cosa fa |
|---|---|
| [[ctl:14]] | Crea una nuova sequenza (vedi sotto). |
| [[ctl:15]] | Elimina le sequenze selezionate. Le chiavi di animazione restano sulla timeline. |
| [[ctl:16]] | Copia le sequenze selezionate. |
| [[ctl:17]] | Apre il *Gestore sequenze globali*. |

## Modificare la sequenza selezionata

| N. | Cosa fa |
|---|---|
| [[ctl:20]] | Il nome. Il nome dice al gioco quando riprodurre l'animazione. Vedi *Nomi delle animazioni*. |
| [[ctl:23]] | Usa il fotogramma corrente del cursore del tempo come fotogramma iniziale. |
| [[ctl:26]] | Il primo fotogramma della sequenza. |
| [[ctl:28]] | Usa il fotogramma corrente come fotogramma finale. |
| [[ctl:31]] | L'ultimo fotogramma della sequenza. |
| [[ctl:32]] | **Rarità** (0–40). Tra animazioni con lo stesso nome, un valore più alto significa che il gioco riproduce questa meno spesso. |
| [[ctl:36]] | **Velocità** (0–700). Quanto veloce si muove l'unità quando riproduce questa animazione di camminata. La maggior parte delle unità ha da 250 a 400. |
| [[ctl:40]] | L'animazione si riproduce una volta e non si ripete. Usalo per *Death*, *Attack* e *Spell*. |
| [[ctl:41]] | L'esportatore tralascia questa sequenza. |
| [[ctl:44]] | Sposta il fotogramma iniziale un fotogramma prima. Il pulsante [[ctl:45]] lo sposta un fotogramma dopo. |
| [[ctl:47]] | Sposta il fotogramma finale un fotogramma prima. Il pulsante [[ctl:48]] lo sposta un fotogramma dopo. |

## Salvare e caricare

| N. | Cosa fa |
|---|---|
| [[ctl:50]] | Carica le sequenze da un file di testo. |
| [[ctl:51]] | Salva le sequenze in un file di testo. Usalo per copiarle in un'altra scena. |

## Creare una sequenza

[[shot:sequence_new|La finestra per una nuova sequenza.]]

1. Clicca [[t:seq_new_btn]].
2. Scegli un preset, per esempio *Walk*. Il preset imposta un nome e una durata adatti.
3. Controlla il nome e i fotogrammi. La nuova sequenza inizia dopo l'ultima.
4. Clicca **OK**.

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | Preset per le animazioni comuni, oppure *Custom*. |
| [[ctl:7]] | Il nome della nuova sequenza. |
| [[ctl:12]] | Primo fotogramma. |
| [[ctl:16]] | Ultimo fotogramma. |
| [[ctl:17]] | Attiva il valore di rarità. |
| [[ctl:21]] | Attiva la velocità di movimento. |
| [[ctl:25]] | L'animazione si riproduce una sola volta. |

!!! tip
    Lascia alcuni fotogrammi vuoti tra due sequenze. Così le chiavi di un'animazione non cambiano quella successiva.

!!! note
    WhiteoutDex salva le sequenze nel tuo file `.max`. Le scene più vecchie con sequenze nelle *note track* funzionano ancora: WhiteoutDex legge anche quelle.
