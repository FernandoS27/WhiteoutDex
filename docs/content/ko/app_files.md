# 파일과 설정

이 페이지는 WhiteoutDex가 파일을 보관하는 위치를 정리합니다. 보통은 이 파일을 열 필요가 없습니다. 문제가 생겼을 때나 새 컴퓨터로 옮길 때 도움이 됩니다.

## 설치 폴더

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| 폴더 또는 파일 | 내용 |
|---|---|
| `native plugins\Max<year>\` | 3ds Max 버전별 플러그인(임포터, 익스포터, 파티클, 리본, 미리보기 창, BLP 리더). |
| `scripts\` | WhiteoutDex 도구(MAXScript). |
| `lang\` | 미리보기 창의 번역. |
| `whiteoutdex_icons\` | 사이드바와 도구의 아이콘. |
| `maps\WhiteoutDex\TeamGlow\` | 팀 글로우 텍스처. |
| `version.txt` | 설치된 버전. |

이 폴더의 파일은 바꾸지 마세요. 업데이트하면 교체됩니다.

## 내 설정

설정은 3ds Max 설정 폴더 **plugcfg**에 있습니다. 이 폴더를 찾으려면 MAXScript Listener([[key:F11]])를 열고 `getDir #plugcfg`를 입력하세요.

| 파일 | 저장하는 내용 |
|---|---|
| `WhiteoutDex_Settings.ini` | 언어, Reforged 폴더, MPQ 순서, 사이드바, 업데이트 확인. |
| `WhiteoutDexMPQ.ini` | 클래식 게임 폴더. |
| `WhiteoutDexImporter.ini` | 마지막 가져오기 옵션. |
| `MDLXExporter.ini` | 마지막 내보내기 옵션. |
| `WhiteoutDex_MaterialFix.ini` | SD와 HD용 *머티리얼 수정* 설정. |

!!! tip
    WhiteoutDex를 처음 시작한 상태로 되돌리려면 3ds Max를 닫고 이 파일을 삭제하세요. WhiteoutDex가 기본값으로 새 파일을 만듭니다.

## 미리보기 창

| 파일 | 저장하는 내용 |
|---|---|
| `WhiteoutFlakes.ini`(`3dsmax.exe` 옆) | WhiteoutFlakes 미리보기 창의 설정. |

## 로그 파일

문제가 생기면 이 파일에 무슨 일이 있었는지 기록됩니다. 파일은 임시 폴더에 있습니다. 임시 폴더를 열려면 [[key:Win+R]]을 누르고 `%TEMP%`를 입력한 뒤 [[key:Enter]]를 누르세요.

| 파일 | 기록하는 곳 |
|---|---|
| `mdlx_import_debug.log` | 임포터 |
| `mdlx_export_debug.log` | 익스포터. 경고의 전체 목록이 있습니다 |
| `WhiteoutDex_render.log` | WhiteoutFlakes 미리보기 창 |

문제를 보고할 때 이 파일을 함께 보내세요.
