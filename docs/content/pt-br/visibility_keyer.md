# Gerador de chaves de visibilidade

No Warcraft III você mostra e oculta partes de um modelo com **chaves de visibilidade**. Exemplos: uma arma que só aparece na animação de ataque, ou um brilho que só aparece enquanto a unidade lança uma magia. O Gerador de chaves de visibilidade define essas chaves para você.

Abra-o com **WhiteoutDex > [[t:menu_visibility_keyer]]**. Selecione os objetos primeiro.

[[shot:visibility_keyer|O Gerador de chaves de visibilidade.]]

| N.º | O que faz |
|---|---|
| [[ctl:3]] | Mostra ou oculta os objetos selecionados no quadro atual. |
| [[ctl:5]] | Adiciona um controlador de visibilidade a todos os objetos selecionados. |
| [[ctl:6]] | Remove os controladores de visibilidade dos objetos selecionados. |
| [[ctl:7]] | Copia as chaves de visibilidade do objeto selecionado. |
| [[ctl:8]] | Cola as chaves nos objetos selecionados. |
| [[ctl:9]] | Como o jogo passa de uma chave para outra: *None* muda na hora, os outros tipos fazem uma transição suave. |
| [[ctl:11]] | Dá aos controladores existentes o tipo que você escolheu. |
| [[ctl:13]] | Define uma chave *mostrar* no quadro atual. |
| [[ctl:14]] | Define uma chave *ocultar* no quadro atual. |
| [[ctl:16]] | Deixa o objeto visível em todo o intervalo de animação. |
| [[ctl:17]] | Oculta o objeto em todo o intervalo de animação. |
| [[ctl:19]] | Mostra o objeto nas sequências que você selecionou no *Gerenciador de sequências*. |
| [[ctl:20]] | Oculta o objeto nas sequências selecionadas. |

## Exemplo: uma arma só para o ataque

1. Selecione a arma.
2. Clique em [[ctl:17]] para ocultá-la em todo lugar.
3. No *Gerenciador de sequências*, selecione as sequências *Attack*.
4. Clique em [[ctl:19]].

!!! tip
    Use a interpolação *None* para a maioria das chaves de visibilidade. Com os outros tipos, o jogo faz o objeto aparecer e sumir aos poucos. Isso fica errado para a maioria das partes.
