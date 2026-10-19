# O exportador

O exportador salva a sua cena como um modelo do Warcraft III (`.mdx` ou `.mdl`). Ele também pode converter as suas texturas para os formatos do jogo.

## Abrir o exportador

- Menu **WhiteoutDex > [[t:menu_exporter]]**
- O botão **Exportar** na barra lateral do WhiteoutDex
- **File > Export** e escolha *Warcraft III MDX/MDL Model*

Escolha um nome de arquivo. Então a janela de exportação abre.

## A janela de exportação

[[shot:exporter|A janela de exportação com a aba Geometria.]]

O alto da janela mostra o nome do arquivo. Abaixo dele você vê se o arquivo é novo ou se já existe um arquivo com este nome.

| N.º | O que faz |
|---|---|
| [[ctl:1001]] | **Nome do modelo.** O nome dentro do arquivo. O jogo não o mostra, mas outras ferramentas mostram. |
| [[ctl:1011]] | Exporta para o jogo clássico (MDX versão 800). As texturas viram arquivos `.blp`. |
| [[ctl:1012]] | Exporta para o Reforged (MDX versão 1800). As texturas viram arquivos `.dds`. |
| [[ctl:1015]] | Mostra as opções de malha (esta imagem). |
| [[ctl:1016]] | Mostra as opções de textura. |
| [[ctl:1021]] | Junta em uma só as malhas que usam o mesmo material. O modelo fica menor e mais rápido. |
| [[ctl:1027]] | Iguala as normais onde duas malhas se tocam. Isto esconde as emendas. Não use esta opção se você usa grupos de suavização para arestas duras. |
| [[ctl:1028]] | Exporta também os ossos e auxiliares que nada usa. Normalmente o exportador os deixa de fora. |
| [[ctl:1032]] | Altera os pesos de skin para que funcionem no jogo clássico. O jogo clássico só usa pesos simples. Só para [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | Quando o arquivo já existe, o exportador adiciona `_1`, `_2` e assim por diante ao nome. O seu arquivo antigo continua lá. |
| [[ctl:1031]] | Abre a pasta do arquivo depois da exportação. |
| [[ctl:1]] | Inicia a exportação. |
| [[ctl:2]] | Fecha a janela sem exportar. |

A caixa verde abaixo das opções diz que o WhiteoutDex não encontrou problemas na sua cena. Quando ele encontra problemas, a caixa fica amarela. Veja *Problemas da cena e Corrigir tudo*.

### Aba Texturas

[[shot:exporter_textures|A aba Texturas, aqui para uma exportação clássica.]]

| N.º | O que faz |
|---|---|
| [[ctl:1060]] | Converte as texturas dos seus materiais para `.blp` (Clássico) ou `.dds` (Reforged) e as salva ao lado do modelo. |
| [[ctl:1071]] | **Só Clássico.** [[t:exp_blp_paletted_item]] ou *JPEG*. JPEG tem mais cores. Arquivos paletizados são menores. Com JPEG você também define a qualidade (1–100). |
| [[ctl:1075]] | Mistura um pouco as cores. Isto esconde os degraus duros entre as cores em texturas paletizadas. |
| [[ctl:1061]] | Cria as versões menores da textura que o jogo usa para modelos distantes. Deixe ligada. |
| [[ctl:1062]] | Substitui os arquivos de textura que já existem. Quando está desligada, o exportador mantém os arquivos existentes. |

Para o **Reforged**, você escolhe o formato DDS: *BC3 (DXT5)* ou *BC7*. O BC7 tem melhor aparência. O BC3 funciona em placas de vídeo mais antigas.

## Clássico ou Reforged?

| | Clássico | Reforged |
|---|---|---|
| Jogo | Warcraft III 1.x e os gráficos clássicos do Reforged | Gráficos do Reforged (HD) |
| Versão MDX | 800 | 1800 |
| Texturas | `.blp` | `.dds` |
| Skin | Pesos simples (use [[t:exp_quantize_skin_weights_chk]]) | Pesos completos |

!!! tip
    O exportador lembra todas as opções para a próxima exportação.
