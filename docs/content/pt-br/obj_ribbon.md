# Emissor de rastros

Um emissor de rastros desenha um **rastro** atrás de um ponto que se move: o rastro de luz do golpe de uma espada, a cauda de um cometa, a fumaça de um foguete. Vincule-o ao osso que se move, por exemplo a arma.

Crie-o com **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Parâmetros do material do rastro

[[shot:obj_ribbon_r1|Parâmetros do material do rastro.]]

| N.º | O que faz |
|---|---|
| [[ctl:1079]] | O material do rastro (um *Wc3Material*). Clique para colocá-lo no Material Editor. |
| [[ctl:1073]] | A textura é uma grade de imagens: número de linhas (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … e de colunas (1, 2, 4, 8 …). |
| [[ctl:1077]] | Qual imagem da grade o rastro mostra. Anime-a para fazer o rastro piscar. |

## Propriedades do rastro

[[shot:obj_ribbon_r2|Propriedades do rastro.]]

| N.º | O que faz |
|---|---|
| [[ctl:1066]] | A cor do rastro. |
| [[ctl:1059]] | Até onde o rastro vai acima do emissor. |
| [[ctl:1067]] | Até onde ele vai abaixo do emissor. Acima + abaixo = largura do rastro. |
| [[ctl:1080]] | Transparência: 0 = invisível, 1 = sólido. Anime até 0 para ocultar o rastro. |
| [[ctl:1069]] | Quantos pedaços de rastro começam por segundo. Mais pedaços fazem um rastro mais suave. |
| [[ctl:1071]] | Quanto tempo um pedaço fica, em segundos. Uma vida mais longa faz um rastro mais longo. |
| [[ctl:1082]] | Puxa o rastro para baixo. |
| [[ctl:1090]] | **Controlador:** como um valor animado muda entre as chaves. Cada valor tem o seu próprio controlador. |

!!! tip
    Para o rastro de uma espada: emissor na lâmina, material *Additive*, alfa 0 a não ser durante o golpe, tempo de vida de cerca de 0,2 segundo.
