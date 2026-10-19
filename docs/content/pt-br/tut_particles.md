# Tutorial: Um efeito de fogo

Nesta lição você coloca fogo em uma tocha com um **Particle Emitter 2**.

## Passo 1: Criar o emissor

1. Escolha *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Clique na viewport para posicioná-lo. Mova-o para o alto da tocha.
3. Vincule-o à tocha (ou ao osso que segura a tocha). Assim ele se move junto.

## Passo 2: A textura

1. Abra o painel **Texture Options**.
2. Clique em **Navegar MPQ / CASC** e escolha uma textura de fogo do jogo (procure por `fire` ou `flare`).
3. Defina **Modo de mistura da textura** como **Additive**. Partículas aditivas brilham.

## Passo 3: O movimento

Em **Emitter Options** e **Timing Options**:

1. **Velocidade:** um valor pequeno. Assim as chamas sobem devagar.
2. **Ângulo do cone:** pequeno. Assim as chamas ficam juntas.
3. **Tempo de vida:** cerca de 1 segundo.
4. **Taxa de emissão:** cerca de 20 partículas por segundo.

Você vê as partículas na viewport na hora. Mude os valores até gostar do fogo.

## Passo 4: Cor e desvanecimento

Em **Particle Options**:

1. **Cor:** início amarelo, meio laranja, fim vermelho-escuro.
2. **Alfa:** início 0, meio 255, fim 0. As chamas aparecem e desaparecem suavemente.
3. **Escala:** início pequeno, meio maior, fim pequeno.

![As Particle Options de um emissor.](obj_particles2_r6.png)

## Passo 5: Verificar

Abra o pré-visualizador (**WhiteoutDex > [[t:menu_renderer]]**). Ele mostra o fogo como o jogo vai mostrar.

!!! tip
    Salve um fogo de que você gosta com **Exportar** em *Particle Configuration*. Carregue-o em outros modelos com **Importar**.
