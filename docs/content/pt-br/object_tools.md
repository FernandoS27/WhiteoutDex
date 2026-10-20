# Ferramentas de manipulação de objetos

Copie e cole posições, vínculos e posições de vértices entre objetos. Bom para modelos simétricos: construa o lado esquerdo e depois copie-o para o lado direito.

Abra as ferramentas com **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|As Ferramentas de manipulação de objetos com uma malha selecionada.]]

## Objetos

| N.º | O que faz |
|---|---|
| [[ctl:5]] | **Hierarquia:** salva os vínculos de pai dos objetos selecionados e os desvincula. |
| [[ctl:6]] | Vincula os objetos de volta aos pais salvos. |
| [[ctl:8]] | **Posição:** copia a posição do primeiro objeto selecionado. |
| [[ctl:9]] | Move os objetos selecionados para a posição copiada. |
| [[ctl:11]] | **Estrutura (E↔D):** copia as posições de todos os objetos selecionados com os seus nomes. |
| [[ctl:12]] | Cola as posições nos objetos com o nome espelhado: uma posição copiada de `Arm_L` vai para `Arm_R`. |
| [[ctl:14]] | Desvincula os objetos por um momento durante a colagem. Assim os filhos deles não se movem junto. |
| [[ctl:15]] | Espelha a posição durante a colagem, nos eixos que você marcar: [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Vértices (Poly Tools)

Primeiro selecione vértices de um Editable Poly ou Editable Mesh. A linha acima dos botões mostra o objeto e quantos vértices você selecionou.

| N.º | O que faz |
|---|---|
| [[ctl:22]] | Copia as posições dos vértices selecionados. |
| [[ctl:23]] | Cola as posições nos vértices. |
| [[ctl:26]] | Espelha os valores X durante a colagem. [[ctl:27]] e [[ctl:28]] fazem o mesmo para Y e Z. |
| [[ctl:29]] | Espelha em torno do centro do mundo. |
| [[ctl:30]] | Espelha em torno do pivô do objeto. |
| [[ctl:31]] | Espelha em torno do centro da seleção. |
| [[ctl:35]] | Cola nos mesmos números de vértice. Use na mesma malha ou em uma cópia dela. |
| [[ctl:36]] | Cola nos vértices que você selecionou, mesmo em outra malha. |

!!! tip
    Dê aos seus objetos nomes com `_L` e `_R` (ou `Left` e `Right`). Assim a *Estrutura (E↔D)* encontra sozinha o par de cada objeto.
