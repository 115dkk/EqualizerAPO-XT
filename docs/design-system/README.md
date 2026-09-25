# 디자인 시스템 소스

Editor의 다섯 스킨(studio, minimal, soft, rack, matrix)을 Design System 아티팩트로 옮긴 소스다. 게시본은 https://claude.ai/artifact/5M3GP6xrdNcQWF3DDAue3p 에 있고, 이 디렉터리는 그 아티팩트의 `project/` 트리와 그것을 다시 만드는 생성기를 보관한다. 정본은 여전히 코드다. 토큰은 `Editor/skins/SkinThemeData.cpp`, 활자와 컨트롤은 각 스킨의 QSS, 철학은 `docs/skins/*.md`이고, 이 트리는 그 값을 손으로 옮긴 것이므로 코드가 바뀌면 여기를 고쳐 다시 게시한다.

## 구성

- `project/README.md`: 브랜드북(한국어). 아티팩트 페이지의 본문이다.
- `project/tokens.json`: 색(다크 먼저, 라이트), 활자, 간격, 라운드 토큰. 색 이름은 `<스킨>-<역할>`이고 공용은 `type-*`, `channel-*`뿐이다.
- `project/skins/*.md`: 스킨별 절(헌법 요약).
- `project/components/<Name>/preview.html`과 `README.md`: 미리보기 19종과 사용 지침. 미리보기는 Qt가 그리는 것을 옮긴 정적 HTML이다.
- `project/components/bundle.css`: 미리보기 스캐폴드. 스킨 토큰을 `--bg --card --accent…` 일반 이름에 매핑하는 `.skin-<id>` 프레임과, 갤러리에서 실측한 카드 행 문법 `.crow`(헤더 40px, 컨트롤 34x30, 배지 44x30, 스킨별 재질)가 여기 있다.
- `project/assets/{Icons,Logos}/README.md`: 자산 묶음의 사용 노트. 파일 자체는 아티팩트의 업로드 저장소에 있고 `asset-ids.txt`가 그 blob id를 적는다.
- `rowlib.py`와 `gen_*.py`: 행 계열 미리보기의 생성기. `rowlib.header()`가 공유 헤더 계약(`[펼침][번호][전원][+][-][편집][종류 배지][제목][요약][채널 배지]`)을 그리고 각 `gen_*.py`가 본문만 답한다. `gen_knob.py`와 `gen_graph.py`는 노브와 분석 그래프의 SVG를 계산한다.
- `stage-assets.py`: 커밋하지 않는 바이너리(폰트, 아이콘 사본, 앱 아이콘 PNG)를 Editor 소스에서 채운다.
- `check.py`, `shots.py`, `contact.py`, `gen_index.py`: 로컬 렌더 점검, 헤드리스 캡처, 갤러리 대조 시트, 인덱스 생성.

## 다시 만들기

```powershell
python docs/design-system/stage-assets.py
```

fontTools(brotli 포함)와 Pillow가 필요하다. 한글 폰트는 아티팩트의 파일당 1 MB 상한 때문에 KS X 1001 완성형과 라틴만 남긴 WOFF2 서브셋으로 만든다.

```powershell
cd docs/design-system
python gen_knob.py; python gen_graph.py; python gen_filtercard.py; python gen_reference.py; python gen_copy.py
python gen_vstbus.py; python gen_selection.py; python gen_comment.py; python gen_geq.py; python gen_logic.py
python gen_filedialog.py; python gen_devsel.py
python check.py        # tokens.json을 tokens.css로 컴파일해 _check/ 아래에 점검 페이지를 만든다
python shots.py        # Playwright로 미리보기 전부를 다크·라이트로 캡처한다(_check/shots/)
```

생성기가 없는 미리보기(Cover, TypeBadge, Controls, Toolbar, FilterPicker, ListChrome, UpdateToast)는 `project/components/<Name>/preview.html`을 직접 고친다. 미리보기 CSS에서 `.lamp`처럼 짧은 클래스 이름은 `bundle.css`의 `.crow.lamp`(studio 시그널 램프 변형)와 충돌해 행이 무너지므로 컴포넌트 전용 접두를 붙인다.

## 갤러리와 대조하기

미리보기의 치수는 오프스크린 갤러리에 맞춘다. MacType 데드락을 피하려면 콘솔 서브시스템 사본(`editbin /SUBSYSTEM:CONSOLE`)으로 실행한다.

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_PLUGIN_PATH = 'Qt\6.10.1\msvc2022_64\plugins'
build-Editor-x64\release\EditorConsole-base.exe --skin-gallery <outDir>          # 135장면 x 5스킨 x 2모드
build-DeviceSelector-x64\release\DeviceSelectorShots.exe --skin-shots <outDir2>  # Device Selector 5스킨 x 2모드 x 5장면
python docs/design-system/contact.py <outDir> include_normal copy_normal          # 장면당 다섯 스킨을 한 장으로
```

## 게시하기

아티팩트에는 Artifact 도구로 `url`에 publish한다(`type_url`을 다시 쓰면 새 아티팩트가 생긴다). 순서는 업로드(자산)가 먼저, 바뀐 파일들이 그다음, 인덱스 `project/design-system.json`이 마지막이다. 인덱스는 게시 직전에 다시 읽어 `lastChange`와 바뀐 키만 고치고 나머지 키와 `createdOnFiles` 마커는 유지한다. `gen_index.py`가 `asset-ids.txt`로 `assetGroups`를 채운 인덱스를 쓴다. 페이지가 생성하는 `tokens.css`, `manifest.json`, `api/`는 쓰지 않는다.
