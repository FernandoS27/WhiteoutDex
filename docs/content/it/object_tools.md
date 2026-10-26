# Strumenti di manipolazione oggetti

Copia e incolla posizioni, collegamenti e posizioni dei vertici tra oggetti. Utile per i modelli simmetrici: costruisci il lato sinistro, poi copialo sul lato destro.

Apri gli strumenti con **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|Gli Strumenti di manipolazione oggetti con una mesh selezionata.]]

## Oggetti

| N. | Cosa fa |
|---|---|
| [[ctl:5]] | **Gerarchia:** salva i collegamenti ai genitori degli oggetti selezionati e li scollega. |
| [[ctl:6]] | Ricollega gli oggetti ai loro genitori salvati. |
| [[ctl:8]] | **Posizione:** copia la posizione del primo oggetto selezionato. |
| [[ctl:9]] | Sposta gli oggetti selezionati nella posizione copiata. |
| [[ctl:11]] | **Struttura (S↔D):** copia le posizioni di tutti gli oggetti selezionati con i loro nomi. |
| [[ctl:12]] | Le incolla sugli oggetti con il nome speculare: una posizione copiata da `Arm_L` va su `Arm_R`. |
| [[ctl:14]] | Scollega gli oggetti per un momento mentre incolla, così i loro figli non si spostano con loro. |
| [[ctl:15]] | Specchia la posizione mentre incolla, lungo gli assi che selezioni: [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Vertici (Strumenti Poly)

Prima seleziona i vertici di un Editable Poly o di un Editable Mesh. La riga sopra i pulsanti mostra l'oggetto e quanti vertici hai selezionato.

| N. | Cosa fa |
|---|---|
| [[ctl:22]] | Copia le posizioni dei vertici selezionati. |
| [[ctl:23]] | Incolla le posizioni sui vertici. |
| [[ctl:26]] | Specchia i valori X mentre incolla. [[ctl:27]] e [[ctl:28]] fanno lo stesso per Y e Z. |
| [[ctl:29]] | Specchia intorno al centro del mondo. |
| [[ctl:30]] | Specchia intorno al pivot dell'oggetto. |
| [[ctl:31]] | Specchia intorno al centro della selezione. |
| [[ctl:35]] | Incolla sugli stessi numeri di vertice. Usalo sulla stessa mesh o su una sua copia. |
| [[ctl:36]] | Incolla sui vertici che hai selezionato, anche su un'altra mesh. |

!!! tip
    Dai ai tuoi oggetti nomi con `_L` e `_R` (o `Left` e `Right`). Così *Struttura (S↔D)* trova da solo il partner di ogni oggetto.
