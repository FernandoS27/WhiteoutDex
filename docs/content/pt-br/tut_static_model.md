# Tutorial: Seu primeiro modelo

Nesta lição você cria um objeto simples do zero: uma caixa de madeira, por exemplo um doodad para um mapa.

## Passo 1: Modelar a caixa

1. Crie um **Box**: *Create > Geometry > Box*. Faça-o com cerca de 60 × 60 × 60 unidades. Uma unidade no 3ds Max é uma unidade no jogo.
2. Posicione-o de modo que fique sobre o chão (base na altura 0) e com o centro em 0, 0.

## Passo 2: Dar um material

1. Abra o Material Editor ([[key:M]]).
2. Escolha um slot livre, clique no botão do tipo de material e escolha **Wc3Material**.
3. Em *Mapas de textura do material*, clique no botão **Difusa** e escolha **Wc3Bitmap**.
4. Clique em **Escolher bitmap...** e escolha uma textura. Ou clique em **Abrir o Navegador de texturas** e pegue uma do jogo (procure por `crate` ou `wood`).
5. Arraste o material para a caixa.

![O Wc3Material com os seus slots de textura.](mat_wc3material_r3.png)

!!! tip
    Você já tem um material Standard ou Physical? A *Correção de materiais* o converte em um Wc3Material para você.

## Passo 3: Colisão

O jogador deve poder clicar na caixa.

1. Selecione a caixa.
2. Abra **WhiteoutDex > [[t:menu_anim_general]]** e clique em **Caixa de colisão de um clique**.

## Passo 4: Uma animação Stand

Todo modelo precisa de pelo menos uma sequência, mesmo que nada se mova.

1. Abra **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Clique em **Nova...**, escolha a predefinição *Stand* e clique em **OK**.

## Passo 5: Exportar e testar

1. Exporte a caixa como `Crate.mdx` (veja a lição *Exportar um modelo*).
2. Olhe a caixa de estado na janela de exportação. Se ela estiver amarela, clique em [[t:exp_fix_all_btn]].
3. Veja o resultado no pré-visualizador: **WhiteoutDex > [[t:menu_renderer]]**.

Você criou o seu primeiro modelo do Warcraft III.
