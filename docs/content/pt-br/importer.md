# O importador

O importador carrega um modelo do Warcraft III (`.mdx` ou `.mdl`) no 3ds Max. Ele também carrega as texturas do modelo do seu jogo.

## Abrir o importador

Use uma destas formas:

- Menu **WhiteoutDex > [[t:menu_importer]]**
- O botão **Importar** na barra lateral do WhiteoutDex
- **File > Import** e escolha o tipo de arquivo *Warcraft III MDX/MDL Model*

Escolha um arquivo. Então a janela de importação abre.

## A janela de importação

[[shot:importer|A janela de importação com a aba Objetos.]]

O alto da janela mostra o nome do arquivo, a versão do jogo do modelo (*Clássico* ou *Reforged*) e quantos ossos, malhas (geosets) e animações ele tem.

| N.º | O que faz |
|---|---|
| [[ctl:1052]] | **Configurações rápidas.** Escolha uma predefinição. Ela ajusta todas as opções abaixo para você. Escolha *Tudo* para importar tudo. As outras predefinições importam menos, por exemplo só a malha sem materiais. |
| [[ctl:1059]] | Apaga a cena aberta e importa o modelo em uma cena vazia. |
| [[ctl:1060]] | Adiciona o modelo à cena aberta. Use isto para combinar modelos. |
| [[ctl:1061]] | Mostra os objetos do modelo (esta imagem). |
| [[ctl:1062]] | Mostra as opções de animação. |
| [[ctl:1063]] | Mostra as opções de malha, materiais e texturas. |
| [[ctl:1021]] | Opção para todos os objetos. Quando está desligada, nenhum objeto é importado. |
| [[ctl:1022]] | O esqueleto. Você precisa dele para animações e para malhas com skin. |
| [[ctl:1023]] | Objetos auxiliares. Eles movem outros objetos, mas você não os vê no jogo. |
| [[ctl:1024]] | Luzes. |
| [[ctl:1025]] | Pontos de anexo, por exemplo para armas e efeitos de magia. |
| [[ctl:1028]] | Objetos de evento, por exemplo sons, pegadas e sangue. |
| [[ctl:1026]] | Emissor de partículas 1: emissores que geram pequenos modelos. |
| [[ctl:1027]] | Emissor de partículas 2: emissores para fogo, fumaça, faíscas e efeitos parecidos. |
| [[ctl:1029]] | Emissores de rastros: rastros, por exemplo atrás de uma espada. |
| [[ctl:1030]] | Formas de colisão (caixas e esferas). O jogo as usa para saber onde você pode clicar no modelo. |
| [[ctl:1031]] | Câmeras, por exemplo a câmera de retrato. |
| [[ctl:1]] | Inicia a importação. |
| [[ctl:2]] | Fecha a janela sem importar. |

O número ao lado de cada tipo de objeto diz quantos objetos desse tipo o modelo tem. Os tipos que o modelo não tem ficam cinza.

!!! note
    Modelos Reforged podem ter mais dois tipos de objeto: emissores *Popcorn FX* e *FaceFX*. Você só os vê quando o modelo os tem.

### Aba Animações

[[shot:importer_animations|A aba Animações.]]

| N.º | O que faz |
|---|---|
| [[ctl:1041]] | Opção para todas as animações. Quando está desligada, o modelo chega sem movimento. |
| [[ctl:1042]] | Movimento (chaves de posição). |
| [[ctl:1043]] | Chaves de rotação. |
| [[ctl:1044]] | Chaves de escala. |
| [[ctl:1048]] | Chaves de mostrar e ocultar. |
| [[ctl:1045]] | Valores animados de objetos, por exemplo a intensidade de uma luz ou a taxa de emissão de partículas. |
| [[ctl:1046]] | Coordenadas de textura que se movem (animação de UV). |
| [[ctl:1047]] | Troca de textura ao longo do tempo (flipbook). |
| [[ctl:1049]] | Mudança de cor ao longo do tempo. |

### Aba Modelo

[[shot:importer_model|A aba Modelo.]]

| N.º | O que faz |
|---|---|
| [[ctl:1001]] | Importa o skin: qual osso move qual vértice. Sem ele, a malha não segue os ossos. |
| [[ctl:1011]] | Cria materiais do WhiteoutDex para o modelo. |
| [[ctl:1012]] | Carrega as texturas. O WhiteoutDex procura nas pastas do seu jogo (veja *Configurações*). |
| [[ctl:1071]] | Importa os auxiliares como objetos *Point*. Quando está desligada, os auxiliares chegam como ossos. |
| [[ctl:1081]] | Solda os vértices que estão no mesmo lugar e deixa a malha mais simples. |
| [[ctl:1082]] | Remove ossos e auxiliares que não têm animação e não são necessários. |

## De onde vêm as texturas

O WhiteoutDex procura cada textura nesta ordem:

1. Na pasta do modelo e nas suas subpastas.
2. Nos arquivos do jogo. Para modelos clássicos (versão 800), ele procura primeiro nos arquivos **MPQ** e depois no **CASC**. Para modelos Reforged, a ordem é a inversa.

A linha de baixo da janela diz quais arquivos ele usa. Você define as pastas do jogo em [[t:menu_settings]].

!!! tip
    O importador salva as texturas que ele tira dos arquivos do jogo ao lado do arquivo do modelo. Assim, na próxima vez, ele as encontra na hora.

## Bom saber

- O importador lembra as suas escolhas para a próxima vez.
- Para importar um modelo do jogo sem um arquivo no seu disco, veja *Importar do jogo*.
