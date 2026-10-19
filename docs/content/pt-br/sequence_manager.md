# Gerenciador de sequências

Um modelo do Warcraft III guarda todas as suas animações em uma só linha do tempo. Cada animação é uma **sequência**: um nome, um quadro inicial e um quadro final. O Gerenciador de sequências cria e altera essas sequências.

Abra-o com **WhiteoutDex > [[t:menu_sequence_manager]]** ou com o botão **Sequências** na barra lateral.

[[shot:sequence_manager_selected|O Gerenciador de sequências com a sequência "Stand - 1" selecionada.]]

## A lista

| N.º | O que faz |
|---|---|
| [[ctl:4]] | Digite uma palavra para mostrar só as sequências que têm essa palavra no nome. |
| [[ctl:6]] | Esvazia o campo de busca e mostra todas as sequências de novo. |
| [[ctl:7]] | Todas as sequências do modelo. Clique para selecionar uma. Segure [[key:Ctrl]] para selecionar mais. **Clique duas vezes** em uma sequência para reproduzi-la na viewport. Linhas como `[GLOBAL 1]` são sequências globais. |
| [[ctl:10]] | Seleciona todas as sequências. |
| [[ctl:11]] | Não seleciona nenhuma. |
| [[ctl:12]] | Seleciona as que não estão selecionadas e desmarca as outras. |

## Criar e remover

| N.º | O que faz |
|---|---|
| [[ctl:14]] | Cria uma nova sequência (veja abaixo). |
| [[ctl:15]] | Apaga as sequências selecionadas. As chaves de animação continuam na linha do tempo. |
| [[ctl:16]] | Copia as sequências selecionadas. |
| [[ctl:17]] | Abre o *Gerenciador de sequências globais*. |

## Alterar a sequência selecionada

| N.º | O que faz |
|---|---|
| [[ctl:20]] | O nome. O nome diz ao jogo quando reproduzir a animação. Veja *Nomes de animação*. |
| [[ctl:23]] | Usa o quadro atual do controle deslizante de tempo como quadro inicial. |
| [[ctl:26]] | O primeiro quadro da sequência. |
| [[ctl:28]] | Usa o quadro atual como quadro final. |
| [[ctl:31]] | O último quadro da sequência. |
| [[ctl:32]] | **Raridade** (0–40). Para animações com o mesmo nome, um valor maior significa que o jogo reproduz esta com menos frequência. |
| [[ctl:36]] | **Velocidade** (0–700). A rapidez com que a unidade se move quando reproduz esta animação de caminhada. A maioria das unidades tem de 250 a 400. |
| [[ctl:40]] | A animação toca uma vez e não se repete. Use para *Death*, *Attack* e *Spell*. |
| [[ctl:41]] | O exportador deixa esta sequência de fora. |
| [[ctl:44]] | Move o quadro inicial um quadro para trás. O botão [[ctl:45]] o move um quadro para a frente. |
| [[ctl:47]] | Move o quadro final um quadro para trás. O botão [[ctl:48]] o move um quadro para a frente. |

## Salvar e carregar

| N.º | O que faz |
|---|---|
| [[ctl:50]] | Carrega sequências de um arquivo de texto. |
| [[ctl:51]] | Salva as sequências em um arquivo de texto. Use isto para copiá-las para outra cena. |

## Criar uma sequência

[[shot:sequence_new|A janela para uma nova sequência.]]

1. Clique em [[t:seq_new_btn]].
2. Escolha uma predefinição, por exemplo *Walk*. A predefinição define um bom nome e uma boa duração.
3. Confira o nome e os quadros. A nova sequência começa depois da última.
4. Clique em **OK**.

| N.º | O que faz |
|---|---|
| [[ctl:4]] | Predefinições para as animações comuns, ou *Custom*. |
| [[ctl:7]] | O nome da nova sequência. |
| [[ctl:12]] | Primeiro quadro. |
| [[ctl:16]] | Último quadro. |
| [[ctl:17]] | Liga o valor de raridade. |
| [[ctl:21]] | Liga a velocidade. |
| [[ctl:25]] | A animação toca só uma vez. |

!!! tip
    Deixe alguns quadros vazios entre duas sequências. Assim as chaves de uma animação não alteram a próxima.

!!! note
    O WhiteoutDex salva as sequências no seu arquivo `.max`. Cenas antigas com sequências em *note tracks* continuam funcionando: o WhiteoutDex também as lê.
