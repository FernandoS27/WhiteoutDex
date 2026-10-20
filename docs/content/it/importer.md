# L'importatore

L'importatore carica un modello di Warcraft III (`.mdx` o `.mdl`) in 3ds Max. Carica anche le texture del modello dal tuo gioco.

## Aprire l'importatore

Usa uno di questi modi:

- Menu **WhiteoutDex > [[t:menu_importer]]**
- Il pulsante **Importa** nella barra laterale di WhiteoutDex
- **File > Import** e scegli il tipo di file *Warcraft III MDX/MDL Model*

Scegli un file. Poi si apre la finestra di importazione.

## La finestra di importazione

[[shot:importer|La finestra di importazione con la scheda Oggetti.]]

In alto la finestra mostra il nome del file, la versione del gioco del modello (*Classico* o *Reforged*) e quante ossa, mesh (geoset) e animazioni ha.

| N. | Cosa fa |
|---|---|
| [[ctl:1052]] | **Impostazioni rapide.** Scegli un preset. Il preset imposta per te tutte le opzioni qui sotto. Scegli *Tutto* per importare tutto. Gli altri preset importano meno, per esempio solo la mesh senza materiali. |
| [[ctl:1059]] | Elimina la scena aperta e importa il modello in una scena vuota. |
| [[ctl:1060]] | Aggiunge il modello alla scena aperta. Usalo per combinare più modelli. |
| [[ctl:1061]] | Mostra gli oggetti del modello (questa immagine). |
| [[ctl:1062]] | Mostra le opzioni delle animazioni. |
| [[ctl:1063]] | Mostra le opzioni per mesh, materiali e texture. |
| [[ctl:1021]] | Interruttore per tutti gli oggetti. Quando è disattivo, nessun oggetto viene importato. |
| [[ctl:1022]] | Lo scheletro. Ti serve per le animazioni e per le mesh con skin. |
| [[ctl:1023]] | Oggetti helper. Muovono altri oggetti, ma non li vedi nel gioco. |
| [[ctl:1024]] | Luci. |
| [[ctl:1025]] | Punti di attacco, per esempio per armi ed effetti degli incantesimi. |
| [[ctl:1028]] | Oggetti evento, per esempio suoni, impronte e sangue. |
| [[ctl:1026]] | Emettitori di particelle 1: emettitori che generano piccoli modelli. |
| [[ctl:1027]] | Emettitori di particelle 2: emettitori per fuoco, fumo, scintille ed effetti simili. |
| [[ctl:1029]] | Emettitori di scie: scie, per esempio dietro una spada. |
| [[ctl:1030]] | Forme di collisione (box e sfere). Il gioco le usa per sapere dove puoi cliccare il modello. |
| [[ctl:1031]] | Telecamere, per esempio la telecamera del ritratto. |
| [[ctl:1]] | Avvia l'importazione. |
| [[ctl:2]] | Chiude la finestra senza importare. |

Il numero accanto a ogni tipo di oggetto ti dice quanti oggetti di quel tipo ha il modello. I tipi che il modello non ha sono grigi.

!!! note
    I modelli Reforged possono avere altri due tipi di oggetto: emettitori *Popcorn FX* e *FaceFX*. Li vedi solo quando il modello li ha.

### Scheda Animazioni

[[shot:importer_animations|La scheda Animazioni.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1041]] | Interruttore per tutte le animazioni. Quando è disattivo, il modello arriva senza movimento. |
| [[ctl:1042]] | Movimento (chiavi di posizione). |
| [[ctl:1043]] | Chiavi di rotazione. |
| [[ctl:1044]] | Chiavi di scala. |
| [[ctl:1048]] | Chiavi che mostrano e nascondono. |
| [[ctl:1045]] | Valori animati degli oggetti, per esempio l'intensità di una luce o il tasso di emissione delle particelle. |
| [[ctl:1046]] | Coordinate delle texture che si muovono (animazione UV). |
| [[ctl:1047]] | Texture che cambiano nel tempo (flipbook). |
| [[ctl:1049]] | Colori che cambiano nel tempo. |

### Scheda Modello

[[shot:importer_model|La scheda Modello.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1001]] | Importa lo skin: quale osso muove quale vertice. Senza skin la mesh non segue le ossa. |
| [[ctl:1011]] | Crea i materiali di WhiteoutDex per il modello. |
| [[ctl:1012]] | Carica le texture. WhiteoutDex cerca nelle cartelle del tuo gioco (vedi *Impostazioni*). |
| [[ctl:1071]] | Importa gli helper come oggetti *Point*. Quando è disattivo, gli helper arrivano come ossa. |
| [[ctl:1081]] | Unisce i vertici che si trovano nello stesso punto e rende la mesh più semplice. |
| [[ctl:1082]] | Rimuove le ossa e gli helper che non hanno animazione e non servono. |

## Da dove vengono le texture

WhiteoutDex cerca ogni texture in questo ordine:

1. Nella cartella del modello e nelle sue sottocartelle.
2. Negli archivi del gioco. Per i modelli classici (versione 800) cerca prima negli archivi **MPQ** e poi in **CASC**. Per i modelli Reforged fa il contrario.

La riga in fondo alla finestra ti dice quali archivi usa. Imposti le cartelle del gioco in [[t:menu_settings]].

!!! tip
    L'importatore salva accanto al file del modello le texture che prende dagli archivi del gioco. Così la volta successiva le trova subito.

## Buono a sapersi

- L'importatore ricorda le tue scelte per la volta successiva.
- Per importare un modello dal gioco senza un file sul tuo disco, vedi *Importare dal gioco*.
