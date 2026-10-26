# 파티클 이미터 1

파티클 이미터 1은 그림이 아니라 **작은 모델**을 내보냅니다. 떨어지는 나뭇잎, 돌, 화살처럼 그 자체가 모델인 것에 사용하세요.

**Create > Geometry > Particle Systems > Wc3Particles1**로 만듭니다.

## Particle Configuration (파티클 구성)

[[shot:obj_particles1_r1|Particle Configuration.]]

| 번호 | 기능 |
|---|---|
| [[ctl:2020]] | `.ini` 파일에서 모든 설정을 불러옵니다. |
| [[ctl:2021]] | 모든 설정을 `.ini` 파일에 저장합니다. |
| [[ctl:2022]] | 켜면 *가져오기*가 값의 애니메이션 키도 불러옵니다. |

## Emitter Options (이미터 옵션)

[[shot:obj_particles1_r2|Emitter Options.]]

| 번호 | 기능 |
|---|---|
| [[ctl:5002]] | 모델이 날아가는 빠르기입니다. |
| [[ctl:5004]] | 방향의 위아래 각도입니다. |
| [[ctl:5006]] | 방향의 옆 각도입니다. |
| [[ctl:5008]] | 모델을 아래로 끌어당깁니다. |
| [[ctl:2030]] | **컨트롤:** 애니메이션된 값이 키 사이에서 바뀌는 방식입니다. 값마다 자기 컨트롤이 있습니다. |

## Timing Options (타이밍 옵션)

[[shot:obj_particles1_r3|Timing Options.]]

| 번호 | 기능 |
|---|---|
| [[ctl:5010]] | 모델 하나가 사는 시간(초)입니다. |
| [[ctl:5012]] | 초당 시작하는 모델의 개수입니다. |

## Model Options (모델 옵션)

[[shot:obj_particles1_r4|Model Options.]]

| 번호 | 기능 |
|---|---|
| [[ctl:5017]] | 게임 안에서 모델이 있는 폴더입니다. |
| [[ctl:5016]] | 모델의 파일 이름입니다. 예: `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | 게임 아카이브에서 모델을 고릅니다. |
| [[ctl:2023]] | 디스크에서 모델 파일을 고릅니다. |
| [[ctl:5014]] | 뷰포트 미리보기에서 모델의 크기입니다. |

!!! note
    클래식 모델은 게임 속 파일이 `.mdx`여도 이 경로에 확장자 `.mdl`을 쓰는 경우가 많습니다. 게임은 둘 다 찾습니다.
