# Emettitore di particelle 1

L'Emettitore di particelle 1 non emette immagini ma **piccoli modelli**. Usalo per cose come foglie che cadono, pietre o frecce che sono a loro volta modelli.

Crealo con **Create > Geometry > Particle Systems > Wc3Particles1**.

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration.]]

| N. | Cosa fa |
|---|---|
| [[ctl:2020]] | Carica tutte le impostazioni da un file `.ini`. |
| [[ctl:2021]] | Salva tutte le impostazioni in un file `.ini`. |
| [[ctl:2022]] | Quando è attivo, *Importa* carica anche le chiavi animate dei valori. |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:5002]] | Quanto velocemente volano via i modelli. |
| [[ctl:5004]] | Angolo della direzione verso l'alto e verso il basso. |
| [[ctl:5006]] | Angolo laterale della direzione. |
| [[ctl:5008]] | Tira i modelli verso il basso. |
| [[ctl:2030]] | **Controllo:** come cambia un valore animato tra le chiavi. Ogni valore ha il suo controllo. |

## Timing Options

[[shot:obj_particles1_r3|Timing Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:5010]] | Quanti secondi vive un modello. |
| [[ctl:5012]] | Quanti modelli partono ogni secondo. |

## Model Options

[[shot:obj_particles1_r4|Model Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:5017]] | La cartella del modello dentro il gioco. |
| [[ctl:5016]] | Il nome del file del modello, per esempio `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Scegli un modello dagli archivi del gioco. |
| [[ctl:2023]] | Scegli un file di modello sul tuo disco. |
| [[ctl:5014]] | Dimensione dei modelli nell'anteprima della viewport. |

!!! note
    I modelli classici spesso scrivono l'estensione `.mdl` in questo percorso, anche quando il file nel gioco è un `.mdx`. Il gioco li trova entrambi.
