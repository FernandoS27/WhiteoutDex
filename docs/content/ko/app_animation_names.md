# 애니메이션 이름

Warcraft III는 애니메이션을 **이름**으로 찾습니다. 공격하는 유닛은 *Attack*으로 시작하는 애니메이션을 재생합니다. 그래서 시퀀스의 이름이 중요합니다.

이름에는 **주 단어**가 있고, **추가 단어**가 붙을 수 있습니다. 예: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## 주 단어

| 이름 | 게임이 재생하는 때 |
|---|---|
| Stand | 유닛이 아무것도 하지 않을 때. |
| Walk | 유닛이 움직일 때. |
| Attack | 유닛이 공격할 때. |
| Spell | 유닛이 주문을 걸 때. |
| Death | 유닛이 죽을 때. |
| Decay | 시체가 썩어 없어질 때(*Decay Flesh*, 그다음 *Decay Bone*). |
| Birth | 건물이 지어지거나 유닛이 나타날 때. |
| Dissipate | 소환된 유닛이나 영혼 유닛이 죽으면서 사라질 때. |
| Portrait | 초상화 창의 얼굴. 유닛이 말하는 동안에는 *Portrait Talk*가 재생됩니다. |
| Morph | 유닛이 형태를 바꿀 때. |
| Sleep | 유닛이 잘 때. |
| Cinematic | 시네마틱과 맵에서만 사용합니다. |

## 추가 단어

| 단어 | 의미 |
|---|---|
| Ready | 싸울 준비가 된 상태. 예: *Stand Ready*. |
| Slam | 강한 타격. 예: *Attack Slam*, *Spell Slam*. |
| Victory | 싸움에서 이긴 뒤. |
| Hit | 유닛이 맞을 때. |
| Channel | 시간이 걸리는 주문. |
| Throw | 무언가를 던질 때. |
| Fast | 더 빠른 버전. 예: *Walk Fast*. |
| Alternate | 유닛의 두 번째 형태. 예: *Metamorphosis* 뒤. |
| Upgrade First … Fifth | 1~5번째 업그레이드 뒤의 건물. |
| Work, Gold, Lumber | 일꾼 애니메이션. |
| Swim | 물속에서 움직일 때. |
| Defend | *Defend*가 켜진 상태. |
| Talk | 말할 때(*Portrait*와 함께). |

## 이름이 같은 애니메이션 여러 개

주 단어가 같은 애니메이션을 여러 개 둘 수 있습니다. 예: `Stand - 1`, `Stand - 2`, `Stand - 3`. 게임은 그중 하나를 무작위로 고릅니다. [[t:menu_sequence_manager]]의 **희귀도** 값은 게임이 그 애니메이션을 얼마나 자주 고를지 알려 줍니다. 값이 높을수록 덜 자주 고릅니다.

## 잘 맞는 길이

[[t:menu_sequence_manager]]에는 다음 길이의 프리셋이 있습니다(초당 30프레임 기준).

| 프리셋 | 프레임 |
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
    *Death*, *Decay*, *Birth*, *Dissipate*는 한 번만 재생됩니다. 시퀀스 관리자에서 **반복 안 함**으로 설정하세요.
