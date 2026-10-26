# Wc3Bitmap

O **Wc3Bitmap** é o mapa de textura do WhiteoutDex. Coloque-o nos slots de um *Wc3Material*. Ele lê `.blp`, `.dds`, `.tga`, `.png` e os outros formatos de imagem do 3ds Max. Ele também guarda a animação de textura (animação de UV) e o caminho da textura no jogo.

## Parâmetros de textura WC3

[[shot:mat_wc3bitmap_r1|Parâmetros de textura WC3.]]

| N.º | O que faz |
|---|---|
| [[ctl:6]] | Escolha um arquivo de imagem no seu disco. |
| [[ctl:7]] | Escolha uma textura dos arquivos do jogo. |
| [[ctl:9]] | Usa uma textura que o jogo preenche (cor de equipe, brilho de equipe, penhasco, árvores). |
| [[ctl:12]] | A textura se repete na largura. |
| [[ctl:13]] | A textura se repete na altura. |
| [[ctl:14]] | Usa a textura como reflexo (sphere environment map). |

## Animação de UV (TXAN)

[[shot:mat_wc3bitmap_r2|Animação de UV.]]

Anime estes valores para fazer a textura se mover. O *Criador de animação de textura* pode defini-los para você.

| N.º | O que faz |
|---|---|
| [[ctl:3]] | Como os valores mudam entre as chaves. |
| [[ctl:8]] | Move a textura para os lados (U). |
| [[ctl:11]] | Move a textura para cima e para baixo (V). |
| [[ctl:14]] | Deslocamento W. |
| [[ctl:17]] | Gira a textura. |
| [[ctl:20]] | Repete a textura mais ou menos vezes na horizontal. |
| [[ctl:23]] | Repete a textura mais ou menos vezes na vertical. |

## Caminho do prefixo

[[shot:mat_wc3bitmap_r3|Caminho do prefixo.]]

O prefixo é a pasta que o exportador escreve na frente do nome da textura, por exemplo `Textures\` ou `war3mapImported\`.

| N.º | O que faz |
|---|---|
| [[ctl:5]] | Escolha um prefixo salvo. |
| [[ctl:6]] | Edite a lista de prefixos salvos. |

!!! tip
    As texturas em um mapa precisam usar o prefixo que o mapa usa. Para modelos personalizados em mapas, normalmente é `war3mapImported\`.
