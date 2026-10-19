# Wc3Material

Toda malha que você exporta precisa de um **Wc3Material**. Ele guarda as texturas e diz ao jogo como desenhá-las. O importador cria Wc3Materials para você. Para as suas próprias malhas, crie um no Material Editor ou converta os seus materiais com a *Correção de materiais*.

As imagens mostram um material do modelo de exemplo no Compact Material Editor.

## Parâmetros básicos do material

[[shot:mat_wc3material_r1|Parâmetros básicos do material.]]

| N.º | O que faz |
|---|---|
| [[ctl:3]] | **Shader.** *SD* para modelos clássicos, *HD* para o Reforged, *SD on HD* para texturas clássicas em um modelo Reforged, *Crystal* para o shader de cristal do Reforged. |
| [[ctl:5]] | **Modo de filtro** – como o material é desenhado (veja a tabela abaixo). |
| [[ctl:8]] | Os dois lados das faces são desenhados. |
| [[ctl:9]] | Ignora a luz. Bom para partes que brilham. |
| [[ctl:10]] | Ignora a névoa. |
| [[ctl:11]] | Uma só cor para a malha inteira. |
| [[ctl:12]] | Define o sinalizador *Full Resolution* do material. |
| [[ctl:13]] | É desenhado por cima de outras coisas. |
| [[ctl:14]] | Não esconde o que está atrás dele. |
| [[ctl:15]] | O jogador não pode clicar nesta parte. |
| [[ctl:16]] | Usa oclusão de ambiente (Reforged). |
| [[ctl:17]] | As faces traseiras projetam sombras (Reforged). |
| [[ctl:18]] | Não recebe nenhuma luz (Reforged). |
| [[ctl:20]] | Ordem de desenho das camadas transparentes: de perto para longe, ou de longe para perto. |
| [[ctl:24]] | Ordem de desenho entre materiais (−20 a 20). Valores maiores são desenhados depois. |
| [[ctl:26]] | Usa uma textura que o jogo preenche: cor de equipe, brilho de equipe, penhasco ou uma das texturas de árvore. |

| Modo de filtro | Aparência | Use para |
|---|---|---|
| None | Sólido. | Partes normais. |
| Transparent | Buracos onde o alfa é baixo, bordas duras. | Folhas, cabelo, cercas. |
| Blend | Transparência suave. | Vidro, bordas suaves, sombras. |
| Additive | Adiciona luz, brilha. O preto é invisível. | Fogo, magia, raios de luz. |
| AddAlpha | Adiciona luz, usa o alfa. | Brilhos com uma forma. |
| Modulate | Escurece o que está atrás. | Marcas escuras, sombras. |
| Modulate 2x | Escurece ou clareia. | Efeitos especiais. |

## Animação da camada

[[shot:mat_wc3material_r2|Animação da camada.]]

| N.º | O que faz |
|---|---|
| [[ctl:6]] | Opacidade da camada (0 a 1). Anime-a para fazer o material aparecer e sumir aos poucos. |
| [[ctl:7]] | Como a opacidade muda entre as chaves. |
| [[ctl:9]] | Escreve a animação de opacidade no modelo. |
| [[ctl:11]] | A textura muda em etapas ao longo do tempo, como um flipbook. |
| [[ctl:14]] | Qual canal UV a textura usa. |
| [[ctl:15]] | Escreve a animação de troca de textura no modelo. |

As texturas que se movem (animação de UV) são definidas no *Wc3Bitmap*, não aqui.

## Mapas de textura do material

[[shot:mat_wc3material_r3|Mapas de textura do material.]]

| N.º | O que faz |
|---|---|
| [[ctl:4]] | **Difusa:** a textura de cor. Os modelos clássicos só usam esta. |
| [[ctl:5]] | Abre o navegador de texturas para este slot (texturas do jogo). |
| [[ctl:10]] | **Normal:** os detalhes da superfície (Reforged). |
| [[ctl:16]] | **ORM:** oclusão, rugosidade e metal em uma só textura (Reforged). |
| [[ctl:22]] | **Emissiva:** partes que brilham sozinhas (Reforged). |
| [[ctl:28]] | **Cor de equipe:** uma máscara para a cor de equipe (Reforged, opcional). |
| [[ctl:34]] | **Ambiente:** a textura de reflexo (Reforged). |

Cada slot tem um **Prefixo do caminho**: a pasta que o exportador escreve na frente do nome do arquivo, por exemplo `Textures\`.

## Configurações do Reforged

[[shot:mat_wc3material_r4|Configurações do Reforged.]]

| N.º | O que faz |
|---|---|
| [[ctl:5]] | A força com que a textura emissiva brilha. |
| [[ctl:9]] | Cor de Fresnel (luz de borda): vermelho … |
| [[ctl:12]] | … verde … |
| [[ctl:15]] | … e azul. |
| [[ctl:18]] | Intensidade da luz de borda. |
| [[ctl:21]] | Quanto da cor de equipe a luz de borda usa. |
