# Tutorial: Animar

Nesta lição você dá duas animações à caixa da lição anterior: *Stand* (ela não faz nada) e *Death* (ela se desmancha).

## Passo 1: Planejar a linha do tempo

Todas as animações de um modelo dividem uma só linha do tempo. Cada animação recebe a sua própria parte:

| Sequência | Quadros |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Deixe alguns quadros livres entre as partes.

## Passo 2: Criar as sequências

1. Abra **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Clique em **Nova...**, escolha *Stand*, defina o início 0 e o fim 80 e clique em **OK**.
3. Clique em **Nova...**, escolha *Death*, defina o início 100 e o fim 164, ligue **Sem loop** e clique em **OK**.

![O Gerenciador de sequências com uma sequência selecionada.](sequence_manager_selected.png)

## Passo 3: Definir chaves

1. Ligue o **Auto Key** no 3ds Max.
2. Defina chaves no início e no fim de cada sequência. Abra **WhiteoutDex > [[t:menu_anim_general]]**, selecione a caixa e clique em **Adicionar chaves inicial e final aos objetos selecionados**.
3. Vá para o quadro 130. Gire a caixa e mova-a para baixo, como se ela tombasse.
4. Vá para o quadro 164 e mova-a para baixo do chão.
5. Desligue o **Auto Key**.

## Passo 4: Testar

1. No Gerenciador de sequências, clique duas vezes em *Death*. O 3ds Max a reproduz.
2. Abra o pré-visualizador (**WhiteoutDex > [[t:menu_renderer]]**) e escolha *Death* na lista **Animação**.

!!! tip
    Ossos animam melhor do que malhas. Vincule as partes de um modelo a ossos e anime os ossos. Depois use um modificador **Skin** nas malhas.

!!! note
    O jogo reproduz *Stand* sem parar. *Death* toca uma vez. Depois o corpo fica no chão até as animações *Decay* (se o modelo as tiver).
