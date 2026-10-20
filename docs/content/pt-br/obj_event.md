# Objeto de evento

Um objeto de evento inicia algo em um momento exato de uma animação: um som quando a espada acerta, uma pegada quando o pé toca o chão, sangue quando a unidade morre, um modelo que aparece.

Crie-o com **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Coloque-o onde o evento deve acontecer. Vincule-o a um osso se ele deve se mover.

Na viewport, cada objeto de evento mostra o seu nome em verde.

[[shot:obj_event_r2|O painel Event Object de um som de morte do modelo de exemplo.]]

## Escolher o evento

| N.º | O que faz |
|---|---|
| [[ctl:5]] | O tipo de evento (veja a tabela abaixo). |
| [[ctl:8]] | O evento exato da lista do jogo, por exemplo um som ou uma cor de sangue. |
| [[ctl:13]] | Usa a sua escolha e dá nome ao objeto. A linha abaixo do botão mostra o código do evento. |

Você também pode digitar um código de evento em *Dados personalizados*.

| Tipo | O código começa com | O que acontece |
|---|---|---|
| FootPrint | `FPT` | Uma pegada no chão. |
| BloodSplat | `SPL` | Uma mancha no chão, por exemplo sangue. |
| UberSplat | `UBR` | Uma marca grande no chão, por exemplo uma cratera ou a base de uma construção. |
| Sound | `SND` | Um som. |
| SpawnObject | `SPN` | Um modelo aparece, por exemplo uma parte do corpo que cai. |

## Quando acontece

| N.º | O que faz |
|---|---|
| [[ctl:19]] | Os momentos em que o evento acontece. |
| [[ctl:20]] | Mova o controle deslizante de tempo até o quadro certo e clique aqui para adicionar um momento. |
| [[ctl:21]] | Remove o momento selecionado. |
| [[ctl:22]] | Remove todos os momentos. |

!!! tip
    Use o *Gerenciador de sequências* para ir até a animação. Depois mova o controle deslizante quadro a quadro para achar o momento exato, por exemplo quando o pé toca o chão.
