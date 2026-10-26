# Generatore chiavi di visibilità

In Warcraft III mostri e nascondi parti di un modello con le **chiavi di visibilità**. Per esempio un'arma che compare solo nell'animazione di attacco, o un bagliore che compare solo mentre l'unità lancia un incantesimo. Il Generatore chiavi di visibilità imposta queste chiavi per te.

Aprilo con **WhiteoutDex > [[t:menu_visibility_keyer]]**. Prima seleziona gli oggetti.

[[shot:visibility_keyer|Il Generatore chiavi di visibilità.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3]] | Mostra o nasconde gli oggetti selezionati nel fotogramma corrente. |
| [[ctl:5]] | Aggiunge un controller di visibilità a tutti gli oggetti selezionati. |
| [[ctl:6]] | Rimuove i controller di visibilità dagli oggetti selezionati. |
| [[ctl:7]] | Copia le chiavi di visibilità dell'oggetto selezionato. |
| [[ctl:8]] | Le incolla sugli oggetti selezionati. |
| [[ctl:9]] | Come il gioco passa da una chiave all'altra: *None* salta subito, gli altri tipi sfumano. |
| [[ctl:11]] | Dà ai controller esistenti il tipo che hai scelto. |
| [[ctl:13]] | Imposta una chiave *mostra* nel fotogramma corrente. |
| [[ctl:14]] | Imposta una chiave *nascondi* nel fotogramma corrente. |
| [[ctl:16]] | Rende visibile l'oggetto per tutto l'intervallo di animazione. |
| [[ctl:17]] | Nasconde l'oggetto per tutto l'intervallo di animazione. |
| [[ctl:19]] | Mostra l'oggetto nelle sequenze che hai selezionato nel *Gestore sequenze*. |
| [[ctl:20]] | Nasconde l'oggetto nelle sequenze selezionate. |

## Esempio: un'arma solo per l'attacco

1. Seleziona l'arma.
2. Clicca [[ctl:17]] per nasconderla ovunque.
3. Nel *Gestore sequenze*, seleziona le sequenze *Attack*.
4. Clicca [[ctl:19]].

!!! tip
    Usa l'interpolazione *None* per la maggior parte delle chiavi di visibilità. Con gli altri tipi il gioco sfuma l'oggetto, e per la maggior parte delle parti questo appare sbagliato.
