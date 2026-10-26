# Nomes de animação

O Warcraft III encontra uma animação pelo seu **nome**. Uma unidade que ataca reproduz a animação que começa com *Attack*. Por isso os nomes das suas sequências são importantes.

Um nome tem uma **palavra principal** e pode ter **palavras extras**. Exemplos: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Palavras principais

| Nome | Quando o jogo a reproduz |
|---|---|
| Stand | A unidade não faz nada. |
| Walk | A unidade se move. |
| Attack | A unidade ataca. |
| Spell | A unidade lança uma magia. |
| Death | A unidade morre. |
| Decay | O corpo morto apodrece (*Decay Flesh*, depois *Decay Bone*). |
| Birth | Uma construção é erguida ou uma unidade aparece. |
| Dissipate | Uma unidade invocada ou espiritual desaparece quando morre. |
| Portrait | O rosto na janela de retrato. *Portrait Talk* toca enquanto a unidade fala. |
| Morph | A unidade muda de forma. |
| Sleep | A unidade dorme. |
| Cinematic | Usada só por cinemáticas e mapas. |

## Palavras extras

| Palavra | Significado |
|---|---|
| Ready | Pronta para lutar, por exemplo *Stand Ready*. |
| Slam | Um golpe forte, por exemplo *Attack Slam* ou *Spell Slam*. |
| Victory | Depois de uma luta vencida. |
| Hit | A unidade é atingida. |
| Channel | Uma magia que leva algum tempo. |
| Throw | Arremessa algo. |
| Fast | Versão mais rápida, por exemplo *Walk Fast*. |
| Alternate | A segunda forma de uma unidade, por exemplo depois de *Metamorphosis*. |
| Upgrade First … Fifth | Uma construção depois da sua 1.ª até a 5.ª melhoria. |
| Work, Gold, Lumber | Animações de trabalhadores. |
| Swim | Move-se na água. |
| Defend | Com *Defend* ligado. |
| Talk | Fala (com *Portrait*). |

## Várias animações com o mesmo nome

Você pode ter mais de uma animação com a mesma palavra principal, por exemplo `Stand - 1`, `Stand - 2` e `Stand - 3`. O jogo escolhe uma ao acaso. O valor de **raridade** no [[t:menu_sequence_manager]] diz ao jogo com que frequência ele escolhe uma animação: um valor maior significa menos vezes.

## Durações que funcionam bem

O [[t:menu_sequence_manager]] tem predefinições com estas durações (a 30 quadros por segundo):

| Predefinição | Quadros |
|---|---|
| Stand | 80 |
| Walk | 28 |
| Stand Ready | 20 |
| Attack | 24 |
| Attack Slam | 30 |
| Spell | 28 |
| Spell Slam | 24 |
| Death | 64 |
| Portrait Talk | 70 |

!!! note
    *Death*, *Decay*, *Birth* e *Dissipate* tocam só uma vez. Defina-as como **Sem loop** no Gerenciador de sequências.
