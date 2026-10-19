# Pré-visualizador WhiteoutFlakes

O **WhiteoutFlakes** mostra o seu modelo do jeito que o Warcraft III o mostra: com os shaders do jogo, a cor de equipe, a iluminação, as partículas e os rastros. Ele segue o 3ds Max ao vivo. Quando você move o controle deslizante de tempo ou altera algo, o pré-visualizador se atualiza.

Abra-o com **WhiteoutDex > [[t:menu_renderer]]** ou com o botão **Renderizador** na barra lateral.

![O WhiteoutFlakes ao lado do 3ds Max 2027, com um modelo Reforged na cor de equipe.](media_1.png)

## Barra de ferramentas

| Elemento | O que faz |
|---|---|
| **Animação** | Reproduz uma sequência do modelo. *Intervalo livre* segue o controle deslizante de tempo do Max. |
| **Câmera** | *Câmera Livre* ou uma das câmeras da cena, por exemplo a câmera de retrato. |
| **Equipe** | A cor do jogador no modelo. |
| **Iluminação** | *No Jogo* (como o jogo), *Tela de Seleção* (como os menus) ou *Dinâmica*. |
| **Sincronizar câmera** | O pré-visualizador segue a viewport ativa do 3ds Max. |
| **Ressincronizar** | Lê a cena inteira de novo, por exemplo depois que você adicionou objetos. |

## Menu

| Menu | O que contém |
|---|---|
| **Exibir** | Mostra ou oculta a grade, as partículas, os rastros e os objetos de evento. *Arte do Warcraft III*: quais texturas usar (*Seguir o modo de renderização*, *Classic*, *Reforged (HD)*, *Definitive*). *Tileset*: o chão. |
| **Depuração** | Marcadores para formas de colisão e luzes, visões especiais (albedo, normais, nível de detalhe, número de luzes …) e a escolha do nível de detalhe. |
| **Configurações** | Fundo, brilho, volume do som, hora do dia, iluminação baseada em imagem, sombras, névoa, o backend gráfico (Direct3D 11, Direct3D 12, Vulkan) e as pastas do jogo. |

## Bom saber

- As partículas e os rastros são simulados como no jogo. Quando você salta no controle deslizante de tempo, o pré-visualizador calcula os efeitos até o novo quadro. Assim eles parecem como se a animação tivesse tocado até ali.
- O pré-visualizador salva as suas configurações em `WhiteoutFlakes.ini`, ao lado de `3dsmax.exe`.
- Se o pré-visualizador não mostra nada ou trava, veja `WhiteoutDex_render.log` (veja *Arquivos e configurações*).

!!! tip
    Escolha *Classic* ou *Reforged (HD)* em **Exibir > Arte do Warcraft III** para ver como o seu modelo fica em cada versão do jogo.
