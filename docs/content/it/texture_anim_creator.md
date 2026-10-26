# Creatore animazione texture

Fa muovere le texture: acqua che scorre, un fuoco che brucia da uno sprite sheet, o un flipbook da molte immagini. Lo strumento scrive le chiavi per te.

Aprilo con **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

Ci sono tre tipi di animazione:

| Tipo | Usalo per |
|---|---|
| **Anim flusso** | La texture scorre, gira o cresce: acqua, lava, energia. |
| **Seq. (singola)** | Un'immagine con molte piccole figure in una griglia (uno *sprite sheet*). Lo strumento le mostra una dopo l'altra. |
| **Seq. (multipla)** | Una cartella con molte immagini. Lo strumento le mostra una dopo l'altra (un *flipbook*). |

[[shot:texture_anim_creator|Il Creatore animazione texture con Seq. (singola).]]

## Scegliere la texture

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | Clicca una mesh nella viewport per usare il suo materiale. |
| [[ctl:5]] | Usa la mesh selezionata. |
| [[ctl:6]] | Usa il materiale attivo nel Material Editor. |
| [[ctl:8]] | Il materiale. |
| [[ctl:11]] | La texture del materiale. La riga sotto mostra il suo nome e la sua dimensione. |
| [[ctl:14]] | Anim flusso. |
| [[ctl:15]] | Seq. (singola). |
| [[ctl:16]] | Seq. (multipla). |

## Seq. (singola): uno sprite sheet

| N. | Cosa fa |
|---|---|
| [[ctl:46]] | Il fotogramma in cui inizia l'animazione. |
| [[ctl:50]] | Quante volte si ripete l'animazione. |
| [[ctl:54]] | Quante figure ci sono in una riga del foglio. |
| [[ctl:58]] | Quante righe ha il foglio. |
| [[ctl:62]] | La prima figura da mostrare. |
| [[ctl:66]] | L'ultima figura da mostrare. |
| [[ctl:68]] | Salta fotogrammi tra due figure (0 = ogni fotogramma mostra una nuova figura). |
| [[ctl:71]] | Avanti o indietro. |
| [[ctl:74]] | Figure al secondo: 10, 15, 30 o 60. |
| [[ctl:77]] | **ON:** l'animazione si ripete sempre come sequenza globale. **OFF:** scorre nella timeline. |
| [[ctl:80]] | **ON:** tiene le chiavi che la texture ha già. **OFF:** le sostituisce. |
| [[ctl:84]] | Trova da solo il numero di colonne e di righe nell'immagine. |
| [[ctl:85]] | Imposta le coordinate UV della mesh su una figura del foglio. Ti serve una sola volta. |
| [[ctl:87]] | **ON:** comprime il modificatore UV nella mesh dopo *Genera mappa UV*. |
| [[ctl:116]] | Scrive l'animazione. La riga sopra mostra quante chiavi avrà e quanto durerà. |

## Anim flusso

[[shot:texture_anim_flow|Anim flusso.]]

1. Imposta il tempo di inizio e di fine.
2. Clicca le frecce per la direzione in cui si muove la texture. Puoi anche farla girare (sinistra, destra) e farla crescere o rimpicciolire (+, −).
3. Scegli *Animazione globale* ON per una texture che si muove sempre.
4. Clicca **Genera animazione**.

## Seq. (multipla)

[[shot:texture_anim_many|Seq. (multipla).]]

1. Scegli la cartella con le immagini (pulsante **...**). Le immagini si usano nell'ordine dei loro nomi.
2. Imposta l'ordine di riproduzione, la velocità e il fotogramma iniziale.
3. Imposta il prefisso del percorso, per esempio `Textures\`. Il modello usa questo percorso per ogni immagine.
4. Clicca **Genera animazione**.

!!! tip
    Le chiavi della texture si trovano sul *Wc3Bitmap* (animazione UV) o sul *Wc3Material* (flipbook). Guarda lì per modificarle più tardi.
