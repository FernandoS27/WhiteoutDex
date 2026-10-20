# Tutorial: un effetto di fuoco

In questa lezione metti un fuoco su una torcia con un **Emettitore di particelle 2**.

## Passo 1: crea l'emettitore

1. Scegli *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Clicca nella viewport per posizionarlo. Spostalo in cima alla torcia.
3. Collegalo alla torcia (o all'osso che tiene la torcia), così si muove con lei.

## Passo 2: la texture

1. Apri il rollout **Texture Options**.
2. Clicca **Sfoglia MPQ / CASC** e scegli una texture di fuoco del gioco (cerca `fire` o `flare`).
3. Imposta **Modalità di fusione della texture** su **Additive**. Le particelle additive brillano.

## Passo 3: il movimento

In **Emitter Options** e **Timing Options**:

1. **Velocità:** un valore piccolo, così le fiamme salgono lentamente.
2. **Angolo del cono:** piccolo, così le fiamme restano unite.
3. **Durata di vita:** circa 1 secondo.
4. **Tasso di emissione:** circa 20 particelle al secondo.

Vedi subito le particelle nella viewport. Cambia i valori finché il fuoco ti piace.

## Passo 4: colore e dissolvenza

In **Particle Options**:

1. **Colore:** all'inizio giallo, a metà arancione, alla fine rosso scuro.
2. **Alfa:** all'inizio 0, a metà 255, alla fine 0. Le fiamme compaiono e scompaiono piano.
3. **Scala:** all'inizio piccola, a metà più grande, alla fine piccola.

![Le Particle Options di un emettitore.](obj_particles2_r6.png)

## Passo 5: controlla il risultato

Apri l'anteprima (**WhiteoutDex > [[t:menu_renderer]]**). Mostra il fuoco come lo mostrerà il gioco.

!!! tip
    Salva un fuoco che ti piace con **Esporta** in *Particle Configuration*. Caricalo su altri modelli con **Importa**.
