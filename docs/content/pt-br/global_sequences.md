# Sequências globais

Uma **sequência global** é uma animação que se repete o tempo todo, não importa qual animação a unidade está reproduzindo. Use-a para coisas que nunca param: um cristal que gira, uma tocha acesa, uma bandeira ao vento.

Cada sequência global tem uma duração em quadros. As chaves de um objeto podem pertencer a uma sequência global em vez da linha do tempo normal.

Abra o Gerenciador de sequências globais com o botão [[t:seq_globals_btn]] no *Gerenciador de sequências*.

[[shot:global_sequences|O Gerenciador de sequências globais com as duas sequências globais do modelo de exemplo.]]

A lista no alto mostra todas as sequências globais: número, quadros, tipo e os objetos que a usam.

| N.º | O que faz |
|---|---|
| [[ctl:7]] | O quadro final (duração) da sequência global selecionada. |
| [[ctl:8]] | Lê a cena de novo. |
| [[ctl:9]] | Remove a sequência global selecionada. |
| [[ctl:15]] | A duração de uma nova sequência global, em quadros. |
| [[ctl:24]] | Cria uma nova sequência global nos objetos selecionados. |
| [[ctl:25]] | Altera a sequência global selecionada. |

## Fazer um objeto girar o tempo todo

1. Selecione o objeto.
2. Defina a **Duração**, por exemplo 60 quadros.
3. Clique em **Nova**. O controle deslizante de tempo vai para o quadro 0. Aparecem as opções de controlador, canais e giro.
4. Escolha o **Controlador**, por exemplo *Linear*, e o eixo.
5. Mova o controle deslizante de tempo até o último quadro de uma volta, por exemplo 60.
6. Clique em **> Horário** ou em **Anti-horário <**.
7. Clique em **Concluir**.

Agora o objeto dá uma volta a cada 60 quadros, em todas as animações.

Você também pode animar à mão. Depois de **Nova**, defina chaves no objeto como de costume, dentro da duração. Depois clique em **Concluir**. O WhiteoutDex move essas chaves para a nova sequência global.

!!! note
    O jogo conta as sequências globais em milissegundos. O WhiteoutDex converte os quadros para você.
