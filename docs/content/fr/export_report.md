# Le rapport d'export

Après un export, WhiteoutDex vous signale uniquement ce qui demande votre attention. Si tout va bien, vous ne voyez rien et le fichier est simplement là.

Si un point demande votre attention, une fenêtre **Exportation WhiteoutDex** s'ouvre :

- La première ligne compte les erreurs et les avertissements : *Exportation terminée avec 0 erreur(s) et 2 avertissement(s).*
- Chaque ligne suivante nomme un objet et le problème, par exemple `AVERTISSEMENT: [Cape] …`.
- 50 lignes au plus sont affichées. La liste complète se trouve dans le fichier `mdlx_export_debug.log` (voir *Fichiers et paramètres*).

## Erreurs et avertissements

| Type | Signification | Ce que vous faites |
|---|---|---|
| **ERREUR** | Un élément n'a pas pu être exporté. Le modèle est peut-être cassé. | Corrigez l'objet nommé et exportez à nouveau. |
| **AVERTISSEMENT** | Le modèle fonctionne, mais un élément peut avoir un autre aspect que dans 3ds Max. | Lisez la ligne. Souvent, la vérification de la scène (*Tout corriger*) le répare. |

!!! tip
    Lancez d'abord [[t:exp_fix_all_btn]] dans la fenêtre d'export. Cette fonction répare les problèmes les plus fréquents avant l'export. Le rapport reste ainsi court.

L'importateur a le même type de rapport, intitulé **Importation WhiteoutDex**.
