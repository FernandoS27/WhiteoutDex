# O relatório de exportação

Depois de uma exportação, o WhiteoutDex diz a você só o que precisa da sua atenção. Quando tudo está certo, você não vê nada e o arquivo simplesmente está lá.

Quando algo precisa da sua atenção, abre uma janela **WhiteoutDex Export**:

- A primeira linha conta os erros e os avisos: *Export completed with 0 error(s) and 2 warning(s).*
- Cada linha seguinte cita um objeto e o problema, por exemplo `WARNING: [Cape] …`.
- No máximo 50 linhas aparecem. A lista completa está no arquivo `mdlx_export_debug.log` (veja *Arquivos e configurações*).

## Erros e avisos

| Tipo | Significado | O que você faz |
|---|---|---|
| **ERROR** | Algo não pôde ser exportado. O modelo pode estar quebrado. | Corrija o objeto citado e exporte de novo. |
| **WARNING** | O modelo funciona, mas algo pode ficar diferente do 3ds Max. | Leia a linha. Muitas vezes a verificação da cena (*Corrigir tudo*) resolve. |

!!! tip
    Use primeiro [[t:exp_fix_all_btn]] na janela de exportação. Ele corrige os problemas mais comuns antes da exportação. Assim o relatório fica curto.

O importador tem o mesmo tipo de relatório, com o título **WhiteoutDex Import**.
