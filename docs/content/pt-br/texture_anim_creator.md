# Criador de animação de textura

Faz as texturas se moverem: água que corre, um fogo a partir de uma sprite sheet ou um flipbook com muitas imagens. A ferramenta escreve as chaves para você.

Abra-o com **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

Há três tipos de animação:

| Tipo | Use para |
|---|---|
| **Anim fluxo** | A textura desliza, gira ou cresce: água, lava, energia. |
| **Seq. (única)** | Uma imagem com muitas imagens pequenas em uma grade (uma *sprite sheet*). A ferramenta as mostra uma depois da outra. |
| **Seq. (várias)** | Uma pasta com muitas imagens. A ferramenta as mostra uma depois da outra (um *flipbook*). |

[[shot:texture_anim_creator|O Criador de animação de textura com Seq. (única).]]

## Escolher a textura

| N.º | O que faz |
|---|---|
| [[ctl:4]] | Clique em uma malha na viewport para usar o material dela. |
| [[ctl:5]] | Usa a malha selecionada. |
| [[ctl:6]] | Usa o material ativo no Material Editor. |
| [[ctl:8]] | O material. |
| [[ctl:11]] | A textura do material. A linha abaixo mostra o nome e o tamanho dela. |
| [[ctl:14]] | Anim fluxo. |
| [[ctl:15]] | Seq. (única). |
| [[ctl:16]] | Seq. (várias). |

## Seq. (única): uma sprite sheet

| N.º | O que faz |
|---|---|
| [[ctl:46]] | O quadro em que a animação começa. |
| [[ctl:50]] | Quantas vezes a animação se repete. |
| [[ctl:54]] | Quantas imagens há em uma linha da sheet. |
| [[ctl:58]] | Quantas linhas a sheet tem. |
| [[ctl:62]] | A primeira imagem a mostrar. |
| [[ctl:66]] | A última imagem a mostrar. |
| [[ctl:68]] | Pula quadros entre duas imagens (0 = cada quadro mostra uma nova imagem). |
| [[ctl:71]] | Para a frente ou para trás. |
| [[ctl:74]] | Imagens por segundo: 10, 15, 30 ou 60. |
| [[ctl:77]] | **Ligada:** a animação se repete o tempo todo como sequência global. **Desligada:** ela roda na linha do tempo. |
| [[ctl:80]] | **Ligada:** mantém as chaves que a textura já tem. **Desligada:** substitui as chaves. |
| [[ctl:84]] | Encontra sozinho o número de colunas e linhas na imagem. |
| [[ctl:85]] | Ajusta as coordenadas UV da malha para uma imagem da sheet. Você precisa fazer isso uma vez. |
| [[ctl:87]] | **Ligada:** colapsa o modificador UV na malha depois de *Gerar mapa UV*. |
| [[ctl:116]] | Escreve a animação. A linha acima mostra quantas chaves ela terá e quanto vai durar. |

## Anim fluxo

[[shot:texture_anim_flow|Anim fluxo.]]

1. Defina o tempo de início e de fim.
2. Clique nas setas da direção em que a textura se move. Você também pode girá-la (esquerda, direita) e fazê-la crescer ou diminuir (+, −).
3. Ligue *Animação global* para uma textura que sempre se move.
4. Clique em **Gerar animação**.

## Seq. (várias)

[[shot:texture_anim_many|Seq. (várias).]]

1. Escolha a pasta com as imagens (botão **...**). As imagens são usadas na ordem dos nomes.
2. Defina a ordem de reprodução, a velocidade e o quadro inicial.
3. Defina o prefixo do caminho, por exemplo `Textures\`. O modelo usa este caminho para cada imagem.
4. Clique em **Gerar animação**.

!!! tip
    As chaves da textura ficam no *Wc3Bitmap* (animação de UV) ou no *Wc3Material* (flipbook). Procure lá para alterá-las depois.
