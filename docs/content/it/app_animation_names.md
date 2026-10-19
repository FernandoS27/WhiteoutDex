# Nomi delle animazioni

Warcraft III trova un'animazione dal suo **nome**. Un'unità che attacca riproduce l'animazione che inizia con *Attack*. Per questo i nomi delle tue sequenze sono importanti.

Un nome ha una **parola principale** e può avere delle **parole aggiuntive**. Esempi: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Parole principali

| Nome | Quando lo riproduce il gioco |
|---|---|
| Stand | L'unità non fa niente. |
| Walk | L'unità si muove. |
| Attack | L'unità attacca. |
| Spell | L'unità lancia un incantesimo. |
| Death | L'unità muore. |
| Decay | Il corpo morto si decompone (*Decay Flesh*, poi *Decay Bone*). |
| Birth | Un edificio viene costruito o un'unità compare. |
| Dissipate | Un'unità evocata o spirituale svanisce quando muore. |
| Portrait | Il viso nella finestra del ritratto. *Portrait Talk* si riproduce mentre l'unità parla. |
| Morph | L'unità cambia forma. |
| Sleep | L'unità dorme. |
| Cinematic | Usato solo da filmati e mappe. |

## Parole aggiuntive

| Parola | Significato |
|---|---|
| Ready | Pronta a combattere, per esempio *Stand Ready*. |
| Slam | Un colpo forte, per esempio *Attack Slam* o *Spell Slam*. |
| Victory | Dopo una battaglia vinta. |
| Hit | L'unità viene colpita. |
| Channel | Un incantesimo che richiede un po' di tempo. |
| Throw | Lancia qualcosa. |
| Fast | Versione più veloce, per esempio *Walk Fast*. |
| Alternate | La seconda forma di un'unità, per esempio dopo *Metamorphosis*. |
| Upgrade First … Fifth | Un edificio dopo il suo 1° fino al 5° miglioramento. |
| Work, Gold, Lumber | Animazioni dei lavoratori. |
| Swim | Si muove nell'acqua. |
| Defend | Con *Defend* attivo. |
| Talk | Parla (con *Portrait*). |

## Molte animazioni con lo stesso nome

Puoi avere più di un'animazione con la stessa parola principale, per esempio `Stand - 1`, `Stand - 2` e `Stand - 3`. Il gioco ne sceglie una a caso. Il valore di **rarità** nel [[t:menu_sequence_manager]] dice al gioco quanto spesso sceglie un'animazione: un valore più alto significa meno spesso.

## Durate che funzionano bene

Il [[t:menu_sequence_manager]] ha dei preset con queste durate (a 30 fotogrammi al secondo):

| Preset | Fotogrammi |
|---|---|
| Stand | 80 |
| Walk | 28 |
| Stand Ready | 20 |
| Attack | 24 |
| Attack Slam | 30 |
| Spell | 28 |
| Spell Slam | 24 |
| Death | 64 |
| Portrait Talk | 70 |

!!! note
    *Death*, *Decay*, *Birth* e *Dissipate* si riproducono una sola volta. Impostale su **Senza loop** nel Gestore sequenze.
