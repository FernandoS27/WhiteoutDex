# Tutorial: Anexos e eventos

Uma unidade no jogo precisa de lugares para efeitos (pontos de anexo) e de momentos para sons e pegadas (objetos de evento). Nesta lição você adiciona os dois a uma unidade.

**Você precisa de:** uma unidade com ossos e uma animação *Walk*, por exemplo o Paladino importado.

## Parte 1: Pontos de anexo

### O jeito rápido

Se os seus ossos têm nomes comuns (*Head*, *Chest*, *Hand*, *Foot* …):

1. Abra **WhiteoutDex > [[t:menu_anim_general]]**.
2. Clique em **Pontos de anexo de um clique**. O WhiteoutDex cria os pontos e os vincula aos ossos.

### À mão

1. Crie um **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Clique em **Selecionar objeto (absorver)** e clique no osso, por exemplo a mão direita. O ponto salta para lá e se vincula ao osso.
3. Em **Parâmetros do ponto de anexo**, escolha o **Tipo** *Hand* e o **Lado** *Right*. Agora o ponto se chama *Hand Right Ref*.

O jogo mostra ali os efeitos de magia na mão direita.

![Parâmetros do ponto de anexo.](obj_attachpoint_r3.png)

**Pontos que toda unidade deve ter:** *Origin*, *Head*, *Chest*, *Overhead*. As armas usam *Hand Left/Right* ou *Weapon*.

## Parte 2: Pegadas

1. Crie um **Wc3 Event Object** e coloque-o no pé esquerdo. Vincule-o ao osso do pé.
2. Em **Tipo de evento**, escolha o tipo *FootPrint*. Em **Dados**, escolha uma pegada, por exemplo uma pequena.
3. Clique em **Confirmar**.
4. Abra a sequência *Walk* no Gerenciador de sequências. Mova o controle deslizante de tempo até o quadro em que o pé esquerdo toca o chão.
5. Clique em **Adicionar**. Repita para cada passo do pé esquerdo.
6. Faça o mesmo para o pé direito com um segundo objeto de evento.

## Parte 3: Um som

1. Crie outro objeto de evento perto da arma.
2. Escolha o tipo *Sound* e um som de golpe em **Dados**. Depois clique em **Confirmar**.
3. Vá para o quadro da animação *Attack* em que a arma acerta. Clique em **Adicionar**.

## Verificar

Abra o pré-visualizador. Com **Exibir > Objetos de Evento** ligado, ele mostra as pegadas e toca os sons.

!!! tip
    Olhe um modelo do jogo (*Importar do jogo*). Copie como a Blizzard posiciona os pontos de anexo e os eventos.
