# Tutorial: Importar um modelo

Nesta lição você carrega um modelo do Warcraft III no 3ds Max e vê o que o WhiteoutDex faz com ele.

**Você precisa de:** um arquivo de modelo. O WhiteoutDex vem com exemplos na pasta `samples` do código-fonte, por exemplo `HeroPaladin.mdx`. Você também pode usar qualquer `.mdx` ou `.mdl` da internet ou dos seus próprios mapas.

## Passo 1: Iniciar a importação

1. Escolha **WhiteoutDex > [[t:menu_importer]]**.
2. Escolha o arquivo `HeroPaladin.mdx` e clique em **Open**.

A janela de importação abre.

![A janela de importação. Ela mostra o que o modelo contém.](importer.png)

## Passo 2: Escolher as opções

1. Em *Configurações rápidas*, escolha **Tudo**.
2. Em *Modo*, escolha **Nova cena**.
3. Clique em **Importar**.

Depois de alguns segundos, o modelo está na sua cena.

![O Paladino importado com os seus pontos de anexo (amarelos) e objetos de evento (verdes).](tut_model_helpers.png)

## Passo 3: Olhar a cena

Agora a cena contém:

- **As malhas** do modelo com *Wc3Materials* e as texturas.
- **Os ossos** que movem as malhas.
- **Pontos de anexo** com nomes amarelos, por exemplo *Hand Right Ref*.
- **Objetos de evento** com nomes verdes, por exemplo as pegadas *FPT…* e os sons *SND…*.
- **Formas de colisão**, **emissores de partículas** e **rastros**, se o modelo os tiver.

## Passo 4: Reproduzir as animações

1. Abra **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Clique duas vezes em uma sequência, por exemplo *Walk*.

O 3ds Max reproduz a animação de caminhada.

!!! tip
    Auxiliares demais na tela? Abra o [[t:menu_node_manager]], marque *Auxiliares / Dummies*, *Anexos* e *Objetos de evento* e clique em **Ocultar**.

**Próxima lição:** veja o modelo no pré-visualizador WhiteoutFlakes.
