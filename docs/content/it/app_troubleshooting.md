# Problemi e soluzioni

## Il menu WhiteoutDex non c'è

- Controlla che la tua versione di 3ds Max sia tra la 2016 e la 2027.
- Installa di nuovo WhiteoutDex mentre 3ds Max è chiuso.
- 3ds Max 2026 e le versioni più recenti richiedono **.NET 8**. Installalo dal sito di Microsoft e avvia di nuovo 3ds Max.

## Il modello non ha texture (è bianco o rosa)

- Apri [[t:menu_settings]] e controlla le cartelle del gioco. La riga di stato deve dire che gli archivi sono stati trovati.
- I modelli classici cercano negli archivi MPQ, i modelli Reforged in CASC. Imposta la cartella del gioco da cui viene il modello.
- Le texture dei modelli personalizzati devono essere accanto al file del modello o in una sottocartella con il percorso che usa il modello (per esempio `Textures\`).

## Nel gioco il modello appare rovesciato

I triangoli sono rivolti dalla parte sbagliata. L'esportatore gira le mesh che sono rivolte contro le loro normali. Se una parte appare ancora sbagliata, selezionala e usa **Flip** sulle sue normali in 3ds Max.

## Il modello è invisibile o tremola al bordo dello schermo

Le **estensioni** (la dimensione di ogni animazione) sono troppo piccole. L'esportatore le misura su ogni fotogramma. Se hai spostato il modello dopo l'importazione, esporta di nuovo. L'esportatore sostituisce le estensioni che non contengono il modello.

## Parti del modello non si muovono con le ossa

- La mesh ha bisogno di un modificatore **Skin** con le ossa.
- Nella finestra di importazione l'opzione *Con skin* deve essere attiva.
- Per i modelli classici attiva *Quantizza pesi skin* nella finestra di esportazione.

## L'esportazione mostra avvisi

Il rapporto di esportazione elenca ciò che richiede la tua attenzione. Ogni riga nomina l'oggetto. L'elenco completo è in `mdlx_export_debug.log` (vedi *File e impostazioni*).

## 3ds Max mostra "Startup Failure Detection"

L'ultima volta 3ds Max non si è chiuso correttamente. Clicca **Continue Without Restore**. Non scegliere *Restore Factory Settings*, a meno che tu non voglia ripristinare tutto 3ds Max.

## Dove posso trovare aiuto?

- **Discord:** il server di WhiteoutDex (il link è in [[t:menu_about]]).
- **Hive Workshop:** la discussione di WhiteoutDex.
- **GitHub:** segnala i bug nella pagina *Issues*. Aggiungi i file di log di *File e impostazioni*.
