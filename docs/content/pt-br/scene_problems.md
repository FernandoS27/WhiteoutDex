# Problemas da cena e Corrigir tudo

Antes de você exportar, o WhiteoutDex verifica a sua cena. Ele procura coisas que quebrariam o modelo no jogo ou fariam a exportação falhar. Você vê o resultado na janela de exportação.

[[shot:exporter_problems|A janela de exportação encontrou três problemas.]]

| N.º | O que faz |
|---|---|
| [[ctl:1092]] | Corrige todos os problemas de uma vez. Isto altera a sua cena. Cada tipo de correção é um passo de desfazer. Assim você pode voltar atrás com [[key:Ctrl+Z]]. |
| [[ctl:1093]] | Abre a lista de todos os problemas. |

## A lista de problemas

[[shot:scene_problems|Cada linha é um problema.]]

| N.º | O que faz |
|---|---|
| [[ctl:2000]] | Os problemas. Clique duas vezes em uma linha para selecionar o objeto na cena. |
| [[ctl:2004]] | Verifica a cena de novo, por exemplo depois que você corrigiu algo à mão. |
| [[ctl:2002]] | Corrige só os problemas que você selecionou na lista. |
| [[ctl:2003]] | Corrige todos os problemas da lista. |
| [[ctl:1]] | Fecha a lista. |

## O que o WhiteoutDex corrige

| Problema | O que [[t:exp_fix_all_btn]] faz |
|---|---|
| Dois objetos têm o mesmo nome | Renomeia os objetos: `_2`, `_3` e assim por diante. O jogo precisa de nomes únicos. |
| Malha vazia | Apaga a malha. |
| Editable Poly | Adiciona um modificador *Edit Mesh*. Assim o exportador recebe triângulos. |
| *Edit Mesh* acima de *Skin* | Colapsa o modificador. Assim o skin continua correto. |
| Controlador que o jogo não consegue ler | Troca-o por um controlador linear. |
| Malha com um material Multi/Sub-Object | Divide a malha. Assim cada parte tem um material. |
| O material não é um Wc3Material | Converte o material. As configurações vêm da *Correção de materiais* (veja essa página). |
| Dois materiais são iguais | Junta os dois em um só. |

!!! tip
    Você não é obrigado a corrigir os problemas. O exportador também funciona sem isso. Mas o modelo pode ficar errado no jogo.
