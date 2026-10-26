# Ponto de anexo

Um ponto de anexo marca um lugar no modelo. O jogo prende coisas ali: a arma de um herói, um efeito de magia na mão, a flecha que acerta o peito, a barra de vida acima da cabeça.

Crie-o com **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Vincule-o ao osso que ele deve seguir.

Na viewport, cada ponto de anexo mostra o seu nome em amarelo.

## Funções gerais

[[shot:obj_attachpoint_r1|Funções gerais.]]

| N.º | O que faz |
|---|---|
| [[ctl:3]] | Clique aqui e depois clique em um objeto na viewport. O ponto de anexo salta para esse objeto e se vincula a ele. |

## Parâmetros do mapa do modelo

[[shot:obj_attachpoint_r2|Parâmetros do mapa do modelo.]]

Um ponto de anexo pode levar o seu próprio modelo, por exemplo uma espada que a unidade sempre segura.

| N.º | O que faz |
|---|---|
| [[ctl:9]] | Escolha o arquivo do modelo no seu disco. |
| [[ctl:10]] | Escolha um modelo dos arquivos do jogo. |

Os dois campos acima dos botões guardam a pasta e o nome do arquivo. O caminho é relativo à pasta do Warcraft III, por exemplo `Textures\Model.mdx`.

## Parâmetros do ponto de anexo

[[shot:obj_attachpoint_r3|Parâmetros do ponto de anexo.]]

O jogo encontra um ponto de anexo pelo seu **nome**. Você monta o nome a partir de quatro partes:

| N.º | O que faz |
|---|---|
| [[ctl:5]] | **Tipo:** o lugar principal: *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* ou *Sprite*. |
| [[ctl:8]] | **Montaria:** para cavaleiros em uma montaria. |
| [[ctl:11]] | **Lado:** *Left* ou *Right*, para mãos e pés. |
| [[ctl:14]] | **Extra:** mais palavras, por exemplo *Rear*, *Alternate*, *RallyPoint*, *Gold*, ou *First* … *Sixth* para construções. |
| [[ctl:22]] | Usa o modelo de *Parâmetros do mapa do modelo*. |
| [[ctl:26]] | Visibilidade (0 a 1). Anime-a para mostrar ou ocultar o modelo anexado. |
| [[ctl:27]] | Mostra a que o caminho do modelo é relativo. |
| [[ctl:34]] | Um número para o ponto de anexo. |
| [[ctl:36]] | Escreve este número no modelo. |

Exemplo: o Tipo *Hand* e o Lado *Right* dão o nome `Hand Right Ref`. O jogo coloca ali um efeito de magia para a mão direita.

!!! tip
    A ferramenta *Pontos de anexo de um clique* em *Anim.: ferramentas gerais* cria os pontos de anexo comuns para você, quando os seus ossos têm nomes conhecidos.
