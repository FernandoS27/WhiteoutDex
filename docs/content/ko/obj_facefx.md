# FaceFX

**FaceFX**는 Reforged 영웅이 말할 때 얼굴을 움직입니다. 입, 눈, 눈썹이 목소리를 따라갑니다. 데이터는 `.facefx_ingame` 파일에 있습니다. FaceFX 오브젝트는 이 파일을 모델에 연결합니다.

**Create > Helpers > Warcraft III Plugins > Wc3 FaceFX**로 만듭니다. FaceFX는 **Reforged** 모델에서만 작동합니다.

[[shot:obj_facefx_r2|FaceFX 설정.]]

| 번호 | 기능 |
|---|---|
| [[ctl:9]] | `.facefx_ingame` 파일을 고릅니다. |
| [[ctl:10]] | 짧은 설명을 보여 줍니다. |

두 필드에는 **타겟 이름**(게임이 이 얼굴에 사용하는 이름)과 **파일 경로**가 들어 있습니다.

!!! note
    임포터는 Reforged 모델의 FaceFX 오브젝트를 씬으로 가져옵니다. 익스포터는 이것을 바꾸지 않고 다시 씁니다.
