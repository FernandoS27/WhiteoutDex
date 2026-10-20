# Wc3 Vertex Color

O modificador **Wc3 Vertex Color** colore uma malha inteira. Você pode animar a cor, por exemplo para fazer um cristal pulsar ou para fazer uma parte brilhar em vermelho quando a unidade é atingida. O exportador o escreve como a cor de *geoset animation* da malha.

Adicione-o pela lista de modificadores: selecione uma malha e escolha **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Parâmetros básicos do modificador.]]

| N.º | O que faz |
|---|---|
| [[ctl:4]] | A cor com que a malha é tingida. O branco deixa a textura como está. Anime-a com o **Auto Key**. |
| [[ctl:7]] | Define o sinalizador *Drop Shadow* da malha. |
| [[ctl:9]] | Usa a cor. Quando está desligada, só o sinalizador é escrito. |

!!! tip
    Para fazer uma parte aparecer e sumir aos poucos, use chaves de visibilidade (*Gerador de chaves de visibilidade*) ou a *Opacidade* do Wc3Material.
