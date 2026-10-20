# Sequenze globali

Una **sequenza globale** è un'animazione che si ripete sempre, qualunque animazione riproduca l'unità. Usala per le cose che non si fermano mai: un cristallo che gira, una torcia che brucia, una bandiera nel vento.

Ogni sequenza globale ha una durata in fotogrammi. Le chiavi di un oggetto possono appartenere a una sequenza globale invece che alla timeline normale.

Apri il Gestore sequenze globali con il pulsante [[t:seq_globals_btn]] nel *Gestore sequenze*.

[[shot:global_sequences|Il Gestore sequenze globali con le due sequenze globali del modello di esempio.]]

L'elenco in alto mostra tutte le sequenze globali: numero, fotogrammi, tipo e gli oggetti che la usano.

| N. | Cosa fa |
|---|---|
| [[ctl:7]] | Il fotogramma finale (la durata) della sequenza globale selezionata. |
| [[ctl:8]] | Legge di nuovo la scena. |
| [[ctl:9]] | Rimuove la sequenza globale selezionata. |
| [[ctl:15]] | La durata di una nuova sequenza globale, in fotogrammi. |
| [[ctl:24]] | Crea una nuova sequenza globale sugli oggetti selezionati. |
| [[ctl:25]] | Modifica la sequenza globale selezionata. |

## Far girare un oggetto di continuo

1. Seleziona l'oggetto.
2. Imposta la **Durata**, per esempio 60 fotogrammi.
3. Clicca **Nuova**. Il cursore del tempo va al fotogramma 0 e compaiono le opzioni per controller, canali e rotazione.
4. Scegli il **Controller**, per esempio *Linear*, e l'asse.
5. Sposta il cursore del tempo sull'ultimo fotogramma di un giro, per esempio 60.
6. Clicca **> Orario** (in senso orario) o **Antiorario <** (in senso antiorario).
7. Clicca **Fine**.

Ora l'oggetto fa un giro in 60 fotogrammi, in ogni animazione.

Puoi anche animare a mano: dopo **Nuova**, imposta le chiavi sull'oggetto come al solito dentro la durata, poi clicca **Fine**. WhiteoutDex sposta queste chiavi nella nuova sequenza globale.

!!! note
    Il gioco conta le sequenze globali in millisecondi. WhiteoutDex converte i fotogrammi per te.
