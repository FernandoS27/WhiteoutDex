# Emissor de partículas 2

O Particle Emitter 2 faz a maioria dos efeitos do Warcraft III: fogo, fumaça, faíscas, poeira, magia. Ele emite muitas pequenas imagens planas (partículas). Cada partícula vive pouco tempo e muda a sua cor, o seu tamanho e a sua transparência enquanto vive.

Crie-o com **Create > Geometry > Particle Systems > Wc3Particles2**. O WhiteoutDex mostra as partículas direto na viewport.

As imagens abaixo mostram o emissor do modelo de exemplo *HeroPaladin*.

## Particle Configuration

[[shot:obj_particles2_r1|Particle Configuration.]]

| N.º | O que faz |
|---|---|
| [[ctl:1030]] | Carrega todas as configurações de um arquivo `.ini`. |
| [[ctl:1031]] | Salva todas as configurações em um arquivo `.ini`. Use isto para copiar um efeito para outro modelo. |
| [[ctl:1032]] | Quando está ligada, *Importar* também carrega as chaves animadas dos valores, não só os valores fixos. |

## Texture Options

[[shot:obj_particles2_r2|Texture Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:1047]] | A pasta da textura dentro do jogo, por exemplo `Textures\`. |
| [[ctl:3075]] | O nome do arquivo da textura. |
| [[ctl:1033]] | Escolha um arquivo de textura no seu disco. |
| [[ctl:1034]] | Escolha uma textura dos arquivos do jogo. |
| [[ctl:1035]] | Como as partículas são desenhadas: *Blend* (transparência normal), *Additive* (brilha, bom para fogo e magia), *Modulate*, *Modulate 2X* (escurece) ou *Alpha Key* (bordas duras). |
| [[ctl:4113]] | As partículas ignoram a luz. |
| [[ctl:4115]] | As partículas ignoram a névoa. |
| [[ctl:4112]] | Ordena as partículas pela distância. Use quando partículas transparentes se sobrepõem de forma errada. |
| [[ctl:4114]] | Usa a cor de equipe ou o brilho de equipe como textura. |
| [[ctl:3094]] | A textura é uma grade de imagens: número de linhas … |
| [[ctl:3096]] | … e de colunas. A partícula mostra as imagens uma depois da outra. |

## Emitter Options

[[shot:obj_particles2_r3|Emitter Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:3048]] | A velocidade com que as partículas saem voando. |
| [[ctl:3050]] | Quanto a velocidade muda de uma partícula para outra (0 = todas iguais). |
| [[ctl:3052]] | O ângulo do cone em que as partículas voam. Um ângulo pequeno faz um feixe estreito, um ângulo grande faz um jato largo. |
| [[ctl:3057]] | Puxa as partículas para baixo (ou para cima, com um valor negativo). |
| [[ctl:1040]] | **Controle:** como um valor animado muda entre as chaves (veja *Glossário*). Cada valor tem o seu próprio controle. |

## Timing Options

[[shot:obj_particles2_r4|Timing Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:3042]] | Quantos segundos uma partícula vive. |
| [[ctl:3053]] | Quantas partículas começam por segundo. Anime até 0 para parar o efeito. |
| [[ctl:1020]] | Emite todas as partículas de uma vez em uma chave, como uma explosão. |

## Size Options

[[shot:obj_particles2_r5|Size Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:3044]] | Largura da área em que as partículas começam. |
| [[ctl:3046]] | Comprimento dessa área. |
| [[ctl:1028]] | As partículas começam ao longo de uma linha em vez de uma área. |

## Particle Options

[[shot:obj_particles2_r6|Particle Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:1023]] | A partícula é uma imagem plana (cabeça). |
| [[ctl:1024]] | A partícula é um rastro esticado atrás do seu movimento (cauda). |
| [[ctl:1025]] | Cabeça e cauda. |
| [[ctl:3073]] | Comprimento da cauda. |
| [[ctl:6027]] | Em que momento da vida a partícula atinge os valores do *meio* (0,5 = metade da vida). |
| [[ctl:6033]] | A cor no início, no meio e no fim da vida. Clique em uma caixa para escolher uma cor. |
| [[ctl:6037]] | Transparência no início, no meio e no fim: 0 = invisível, 255 = sólido. |
| [[ctl:6041]] | Tamanho no início, no meio e no fim. |
| [[ctl:6045]] | Quais imagens da grade de textura a cabeça mostra enquanto vive … |
| [[ctl:6049]] | … e enquanto desaparece. |
| [[ctl:6053]] | O mesmo para a cauda enquanto vive … |
| [[ctl:6057]] | … e enquanto desaparece. |

## Other Options

[[shot:obj_particles2_r7|Other Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:3059]] | Ordem de desenho. Valores maiores são desenhados depois (por cima). |
| [[ctl:4116]] | As partículas se movem com o modelo. Sem esta opção, elas ficam onde começaram. |
| [[ctl:4117]] | As partículas ficam deitadas no chão em vez de virar para a câmera. Bom para anéis e ondas. |

!!! tip
    Para fogo e magia, use *Additive*, um ângulo de cone pequeno e um tempo de vida curto. Deixe o alfa ir de 0 a 255 e voltar a 0. Assim as partículas aparecem e somem suavemente.
