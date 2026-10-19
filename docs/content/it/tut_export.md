# Tutorial: esportare un modello

Ora salvi di nuovo il modello come file di Warcraft III.

**Ti serve:** la scena della lezione *Importare un modello*.

## Passo 1: avvia l'esportazione

1. Scegli **WhiteoutDex > [[t:menu_exporter]]**.
2. Scegli una cartella e un nome di file, per esempio `MyPaladin.mdx`. Clicca **Salva**.

Si apre la finestra di esportazione.

![La finestra di esportazione. È selezionato Classico.](exporter.png)

## Passo 2: scegli il gioco

- Per il gioco classico scegli **Classico**. Le texture diventano `.blp`.
- Per Reforged scegli **Reforged**. Le texture diventano `.dds`.

Il Paladino è un modello classico, quindi scegli **Classico**.

## Passo 3: controlla la scena

Guarda il riquadro sopra i pulsanti:

- **Verde – Nessun problema:** vai avanti.
- **Giallo – problemi:** clicca [[t:exp_fix_all_btn]]. WhiteoutDex ripara la scena. Vedi *Problemi della scena e Correggi tutto*.

## Passo 4: esporta

1. Nella scheda *Texture*, controlla che *Converti le texture durante l'esportazione* sia attivo.
2. Clicca **Esporta**.

Ora il file `MyPaladin.mdx` e le sue texture sono nella tua cartella. Se qualcosa richiede la tua attenzione, si apre il rapporto di esportazione (vedi *Il rapporto di esportazione*).

## Passo 5: provalo nel gioco

1. Copia il modello e le sue texture nella cartella della tua mappa, oppure importali con l'**Import Manager** del World Editor.
2. Nel World Editor, assegna il tuo modello a un'unità nell'*Object Editor* (campo *Art – Model File*).
3. Prova la mappa.

!!! tip
    L'esportatore ricorda tutte le opzioni. La prossima esportazione dello stesso modello richiede un solo clic.
