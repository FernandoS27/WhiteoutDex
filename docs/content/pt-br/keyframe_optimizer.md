# Otimizador de quadros-chave

Animações de outros programas ou de captura de movimento muitas vezes têm uma chave em cada quadro. Isso deixa o modelo grande. O Otimizador de quadros-chave remove as chaves que não são necessárias, e o movimento fica quase igual.

Abra-o com **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Selecione os objetos animados primeiro.

[[shot:keyframe_optimizer|O Otimizador de quadros-chave.]]

## Configurações

| N.º | O que faz |
|---|---|
| [[ctl:7#2]] | **Limite:** quanto a curva pode mudar. Um valor maior remove mais chaves. Comece com 0,5. |
| [[ctl:11#2]] | **Passo:** com que precisão a ferramenta mede, em quadros. Fique abaixo de 0,49. |
| [[ctl:13#2]] | Escreve detalhes no MAXScript Listener. Deixe desligado, porque é mais lento. |

## Otimizar

| N.º | O que faz |
|---|---|
| [[ctl:3]] | Ordena a lista por tempo ou por nome. |
| [[ctl:5]] | As sequências do modelo. Selecione as que você quer otimizar. |
| [[ctl:7]] | Otimiza as chaves de posição. |
| [[ctl:8]] | Otimiza as chaves de rotação. |
| [[ctl:9]] | Otimiza as chaves de escala. |
| [[ctl:14]] | Otimiza as sequências selecionadas. |
| [[ctl:15]] | Otimiza todas as sequências. |
| [[ctl:16]] | Apaga as chaves que não estão dentro de nenhuma sequência. Elas não fazem nada no jogo. |

!!! tip
    Salve a sua cena antes de otimizar. Confira a animação depois. Se algo ficar errado, desfaça e tente um limite menor.
