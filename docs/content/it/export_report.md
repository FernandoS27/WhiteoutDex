# Il rapporto di esportazione

Dopo un'esportazione, WhiteoutDex ti dice solo ciò che richiede la tua attenzione. Quando tutto va bene, non vedi niente e il file è semplicemente lì.

Quando qualcosa richiede la tua attenzione, si apre una finestra **Esportazione WhiteoutDex**:

- La prima riga conta gli errori e gli avvisi: *Esportazione completata con 0 errore/i e 2 avviso/i.*
- Ogni riga successiva nomina un oggetto e il problema, per esempio `AVVISO: [Cape] …`.
- Si vedono al massimo 50 righe. L'elenco completo è nel file `mdlx_export_debug.log` (vedi *File e impostazioni*).

## Errori e avvisi

| Tipo | Significato | Cosa fai |
|---|---|---|
| **ERRORE** | Qualcosa non si è potuto esportare. Il modello può essere rotto. | Correggi l'oggetto nominato ed esporta di nuovo. |
| **AVVISO** | Il modello funziona, ma qualcosa può apparire diverso rispetto a 3ds Max. | Leggi la riga. Spesso il controllo della scena (*Correggi tutto*) la ripara. |

!!! tip
    Prima esegui [[t:exp_fix_all_btn]] nella finestra di esportazione. Ripara i problemi più comuni prima dell'esportazione, così il rapporto resta breve.

L'importatore ha lo stesso tipo di rapporto, con il titolo **Importazione WhiteoutDex**.
