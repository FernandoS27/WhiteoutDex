# Correção de materiais

O exportador só consegue ler o **Wc3Material**. A Correção de materiais converte outros materiais (Standard, Physical, PBR) em Wc3Materials. Ela mantém as texturas e aplica as opções do Warcraft III que você escolhe aqui.

As mesmas configurações são usadas por [[t:exp_fix_all_btn]] na janela de exportação.

Abra-a com **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|A Correção de materiais com as configurações para modelos clássicos (SD).]]

Há dois conjuntos de configurações: um para modelos clássicos (**SD**) e um para o Reforged (**HD**).

| N.º | O que faz |
|---|---|
| [[ctl:4]] | Configurações para modelos clássicos. |
| [[ctl:5]] | Configurações para modelos Reforged. |

## Renderização

| N.º | O que faz |
|---|---|
| [[ctl:8]] | Como o material é desenhado. *Automático* deixa transparentes os materiais com canal alfa. Veja *Wc3Material* para todos os modos. |
| [[ctl:10]] | Os dois lados das faces são desenhados. |
| [[ctl:11]] | O material ignora a luz. |
| [[ctl:12]] | O material ignora a névoa. |
| [[ctl:13]] | Usa uma só cor para a malha inteira. |
| [[ctl:14]] | É desenhado por cima de tudo (sem teste de profundidade). |
| [[ctl:15]] | Não esconde o que está atrás dele. |
| [[ctl:16]] | Usa oclusão de ambiente (só Reforged). |
| [[ctl:17]] | As faces traseiras projetam sombras (só Reforged). |
| [[ctl:18]] | Pega *face dupla*, *sem sombreamento* e *transparência* do material antigo, em vez das caixas acima. |

## Texturas

| N.º | O que faz |
|---|---|
| [[ctl:23]] | A pasta que fica na frente dos nomes das texturas no modelo, por exemplo `Textures\`. |
| [[ctl:26]] | Como as texturas se repetem: como no material antigo, nas duas direções, só em U, só em V ou nunca. |
| [[ctl:28]] | Um mapa normal ou de relevo vira o mapa *Normal* (Reforged). |
| [[ctl:29]] | Um mapa de emissão vira o mapa *Emissive* (Reforged). |

## Corrigir tudo e converter agora

| N.º | O que faz |
|---|---|
| [[ctl:32]] | Quais configurações [[t:exp_fix_all_btn]] usa na janela de exportação: SD ou HD. |
| [[ctl:34]] | Adiciona `_wc3` ao nome de cada material convertido. |
| [[ctl:39]] | Converte agora os materiais dos objetos selecionados. |
| [[ctl:40]] | Converte agora todos os materiais da cena. |
| [[ctl:42]] | Volta esta aba aos valores padrão. |
| [[ctl:43]] | Fecha a janela. As suas configurações são salvas. |

!!! tip
    Salve a sua cena antes de converter a cena inteira.
