# Emissor de partículas 1

O Particle Emitter 1 não emite imagens, mas **pequenos modelos**. Use-o para coisas como folhas que caem, pedras ou flechas que são modelos.

Crie-o com **Create > Geometry > Particle Systems > Wc3Particles1**.

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration.]]

| N.º | O que faz |
|---|---|
| [[ctl:2020]] | Carrega todas as configurações de um arquivo `.ini`. |
| [[ctl:2021]] | Salva todas as configurações em um arquivo `.ini`. |
| [[ctl:2022]] | Quando está ligada, *Importar* também carrega as chaves animadas dos valores. |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:5002]] | A velocidade com que os modelos saem voando. |
| [[ctl:5004]] | Ângulo da direção para cima e para baixo. |
| [[ctl:5006]] | Ângulo lateral da direção. |
| [[ctl:5008]] | Puxa os modelos para baixo. |
| [[ctl:2030]] | **Controle:** como um valor animado muda entre as chaves. Cada valor tem o seu próprio controle. |

## Timing Options

[[shot:obj_particles1_r3|Timing Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:5010]] | Quantos segundos um modelo vive. |
| [[ctl:5012]] | Quantos modelos começam por segundo. |

## Model Options

[[shot:obj_particles1_r4|Model Options.]]

| N.º | O que faz |
|---|---|
| [[ctl:5017]] | A pasta do modelo dentro do jogo. |
| [[ctl:5016]] | O nome do arquivo do modelo, por exemplo `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Escolha um modelo dos arquivos do jogo. |
| [[ctl:2023]] | Escolha um arquivo de modelo no seu disco. |
| [[ctl:5014]] | Tamanho dos modelos na pré-visualização da viewport. |

!!! note
    Modelos clássicos muitas vezes escrevem a extensão `.mdl` neste caminho, mesmo quando o arquivo no jogo é um `.mdx`. O jogo encontra os dois.
