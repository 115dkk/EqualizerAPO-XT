EqualizerAPO-XT의 Editor는 하나의 카드 문법 위에 다섯 스킨을 입는 Qt 데스크톱 앱이다. 이 시스템은 그 다섯 스킨(`studio` Studio Glass, `minimal` Precision Minimal, `soft` Soft Lab, `rack` Hardware Rack, `matrix` Signal Matrix)의 토큰, 활자, 간격, 상태 문법, 아이콘을 코드에서 그대로 옮긴 것이다. 코드의 정본은 `Editor/skins/SkinThemeData.cpp`(토큰 표), 각 스킨의 QSS 시트, `docs/skins/`의 헌법 문서이고, 문서와 코드가 어긋나면 코드를 믿되 '하지 말 것' 절은 코드보다 오래 산다.

## 시스템을 쓰는 법

색은 언제나 `<스킨>-<역할>` 토큰으로 쓴다. `studio-accent`, `minimal-ink-bright`, `soft-on-ink`, `rack-seam`, `matrix-card-rail`처럼 스킨 접두사가 붙은 이름만 존재하고, 스킨을 가로지르는 공용 색은 명령 타입 색 `type-*`와 채널 식별 색 `channel-*` 두 묶음뿐이다. 다크가 첫 테마이고 라이트가 둘째 테마다. 테마별로 값을 하드코딩하지 않는다. 모드 분기가 꼭 필요하면 배경 명도로 추정한다(코드의 `skinIsDark`).

새 요소를 그릴 때는 다섯 스킨이 각자의 철학으로 따로 답해야 한다. 두 스킨이 팔레트만 다르면 차별화 게이트에서 실패다. 심사 항목은 타입 표시, 호버, 비활성, Include 행, VST 행, 모서리·엣지 언어, 위계의 주도자 일곱 가지다. 각 스킨의 답은 이 시스템의 스킨별 절(Studio Glass, Precision Minimal, Soft Lab, Hardware Rack, Signal Matrix)에 있고, 그 절의 '새 요소를 이 스킨답게 만드는 법' 물음에 답하는 식으로 설계한다.

상호작용은 스킨이 소유하지 않는다. 노브의 드래그·휠·키보드, 픽커의 팝업, Copy의 파싱은 공용이고 스킨은 표현만 바꾼다. 다만 제스처 어휘 가운데 자기 계기에 맞는 것을 고르는 일은 스킨의 몫이다. 회전 호에는 각도 추적, 굴리는 드럼에는 세로 드래그(`knob-travel` 200px가 전 범위, Shift는 1/10)가 맞는다.

## 색

다섯 스킨은 같은 역할 집합을 각자의 재질로 채운다. 역할은 `bg`(창 그라운드), `surface`(툴바·메뉴·패널), `card`, `card-hover`, `card-selected`, `text`, `muted`, `border`, `graph`, `grid-minor`, `grid-major`, `accent`, `accent2`, `success`, `warning`, `danger`, `focus`, `on-accent`이다. `grid-major`는 언제나 `border`, `focus`는 언제나 `accent`, `surface-raised`는 `card-hover`, `surface-sunken`은 `graph`다(코드의 `finishTokens`가 파생한다).

액센트의 뜻은 스킨마다 다르다. studio의 액센트는 광원이라 노브 호와 램프와 글로우가 그 색을 들고, BiQuad 행에서는 필터 가족에 따라 `studio-band-shelf`(민트), `studio-band-pass`(보라), `studio-band-notch`(로즈)로 바뀌며 한 행은 항상 한 색의 빛만 든다. minimal의 액센트 `minimal-accent`는 드래그·입력 중인 컨트롤에만 켜지고 평소 화면은 무채색이다. soft의 액센트는 토큰 자체가 파스텔이라(`soft-accent`, `soft-success`, `soft-warning`, `soft-danger`) 켜진 알약은 불투명 파스텔 채움에 `soft-on-ink`를 얹는다. rack의 색은 램프와 각인에만 산다. `rack-accent`(앰버)가 활성 램프, `rack-accent2`(LED 녹색)가 가동 램프와 LCD 세그먼트다. matrix는 색 배급제라서 `matrix-success` 녹은 가동·저장됨, `matrix-warning` 앰버는 바이패스·수정됨, `matrix-danger` 적은 음수·위험, `matrix-accent` 시안은 체결·선택·라우팅된 신호 데이터, `matrix-accent2` 녹청은 바이폴라 컷 방향에만 쓴다. 이 다섯 줄 밖의 색은 장식이고 금지다.

명령 타입 색 `type-biquad`, `type-include`, `type-vst`, `type-copy`, `type-if` 등은 카탈로그(`FilterCommandCatalog.cpp`)의 색이다. studio는 이 색을 점등된 유리 칩에, soft는 softPastelize(색조 유지, 채도 상한 0.50/0.55, 명도 0.62/0.60)로 파스텔 선반에 올려 스타디움 칩에 쓴다. minimal은 외곽선만, rack은 와이어프레임 안의 픽토그램 잉크로만, matrix는 아예 무시하고 보드 잉크 단색으로 그린다.

채널 식별은 데이터이지 장식이 아니다. 라우팅과 헤더 채널 범위는 `channel-l`부터 `channel-sbr`까지의 고정 색을 쓰고, minimal은 그 색을 콘솔 잉크 표 `minimal-ch-*`로 바꿔 채움 없이 맨 잉크로 인쇄한다. 헤더의 채널 배지 위젯(`ChBadge`)도 같은 표를 쓴다. 한 채널은 어디서 불리든 한 색이고, 표는 `Editor/widgets/routing/ChannelIdentity.h` 하나에 있다(감사 #348 B5, 메인테이너 결정). 표 밖의 채널(ALL, 번호 채널)은 `channel-neutral`을 쓴다.

대비는 소스 값 그대로 기록했다. `minimal-muted`는 다크 그라운드에서 4.4:1로 4.5:1에 못 미치고, `studio-muted`와 `matrix-muted`는 카드 위에서 통과한다. 새로 고르는 잉크는 자기 그라운드에서 4.5:1을 넘긴다.

## 활자

본문은 `body`(11pt, 14.67px, Medium 500)다. 2026-08-23의 활자 계약으로 다섯 스킨의 모든 pt 크기가 한 단 올라갔고 기본 굵기가 Medium이 되었다. rack만 `body-rack`(10pt)으로 한 단 작다. 라틴은 `EAPO Sans`(DM Sans의 개명본)와 `EAPO Mono`(DM Mono), 한글은 `EAPO Sans KR`(Pretendard)와 `EAPO Mono K`(Sarasa Mono K)다. 가족명을 바꾼 이유는 Qt가 같은 이름의 시스템 폰트와 병합해 굵기가 뒤틀리기 때문이고, 이 시스템의 폰트 파일도 그 이름을 쓴다. 한글 파일은 KS X 1001 완성형과 라틴만 남긴 서브셋이다.

위계의 주도자는 스킨마다 다르다. studio는 명도가 주도하고 굵기가 보조한다. minimal은 크기가 하나이고 잉크 밝기(`minimal-ink-bright` 수치, `minimal-text` 본문, `minimal-muted` 캡션)만으로 위계를 만들며 캡션은 대문자와 자간으로 구분한다. soft는 크기와 여백이 주도해 제목이 `title`(12pt SemiBold)이고 두 줄 행이 허용된다. rack은 각인이라 라벨을 `engraved`(9pt Bold 대문자 자간 1px)로 쓰고 각인 문구(`MODULE SELECT`, `NO SIGNAL`, `EAPO-XT SERIES`)는 하드웨어 인쇄물이므로 번역하지 않는다. matrix는 격자 위치와 균일한 활자가 위계를 만들고 좌표는 `coordinate`(9pt 자간 2px)다.

수치는 모노다. 권위 있는 판독값은 `readout`(10pt Bold)이고, minimal은 0패딩으로 폭을 고정하며 좁은 폭에서는 잘라내지 않고 폰트를 줄인다. 활자는 판이나 창의 왼쪽 가장자리에서 시작하지 않고 한 칸 들여 시작한다.

## 간격과 라운드

행 높이는 `minimal-row-height` 32px, `studio-row-height`·`rack-row-height`·`matrix-row-height` 36px, `soft-row-height` 44px다. 채널 그룹 들여쓰기는 16px(minimal, rack), 18px(studio), 20px(soft), 24px(matrix)이고, `card-padding` 12px, `card-gap` 8px, `toolbar-height` 36px는 공유 기본값이다. matrix의 모든 치수는 `matrix-grid-pitch` 24px의 배수다.

모서리 언어가 곧 스킨의 경계다. studio는 `studio-radius` 8px 하나(8이 아닌 값 도입 금지), soft는 카드 `soft-radius-card` 14px, 컨트롤 `soft-radius-control` 10px, 작은 요소는 `soft-radius-pill` 스타디움, rack은 `rack-radius` 3px의 모따기, minimal과 matrix는 0이다. 분석 그래프 프레임은 `graph-radius` 10px다.

## 상태 문법

상태는 한 번만 말한다. 램프가 판정을 들고 있으면 옆에 같은 뜻의 단어를 붙이지 않는다. 문제는 그 스킨의 상태 문법이 먼저 말하고 문장은 사실과 결과만 짧게 잇는다. 어느 스킨도 비활성에 경고색을 입히지 않는다.

- **studio:** 상태는 빛의 세기다. 휴지<호버<드래그의 휘도 사다리를 오르고 비활성은 소등(불투명도 0.35, 램프·반사광·글로우 소거)이다. 포커스는 빛의 윤곽이지 도형의 윤곽이 아니다.
- **minimal:** 상태는 배경 명도 스텝이다. 호버는 `minimal-card`에서 `minimal-card-hover`로 정확히 한 스텝, 비활성은 `minimal-surface`로 한 스텝 아래에 행머리 `#` 주석 마커, 선택과 커서는 본문 잉크 헤어라인 프레임과 반전 블록이다.
- **soft:** 호버는 고도 한 스텝 상승, 포커스는 조용한 할로(α 90, 3px), 비활성은 점선 윤곽과 창 배경 침강과 뮤트 잉크의 삼중 조합인 '잠든 슬롯'이다. 점선 단독은 아직 아무도 보증하지 않는 실체를 뜻한다.
- **rack:** 상태는 램프이고 모든 램프는 베젤 링과 돔과 스펙큘러의 paintLed 문법을 거친다. 비활성은 전원을 내린 유닛이라 LED를 끄고 어두운 필름을 덮되 나사와 각인은 남긴다. 빈 결과는 `NO SIGNAL` 각인이다.
- **matrix:** 호버는 행 밴드와 칼럼 밴드가 교차하는 크로스포인트 예고, 선택은 셀 체결(액센트 LED 채움 + 1px 액센트 룰), 비활성은 결항 처리된 출발편(점선 룰 + 속 빈 앰버 램프 + 취소선)이라 행은 보드에 남는다.

## 카드와 리스트의 공유 계약

헤더 컨트롤은 번호 바로 뒤의 고정 왼쪽 열에 서고 순서는 `[펼침][번호][전원][+][-][편집][종류 배지][제목][요약][채널 배지][여백]`이다. 구조화된 행의 요약은 비어 있고 원문(raw)은 카드에 인쇄하지 않으며 헤더의 코드 마크(`</>`)가 연다. 헤더 `+`는 이 카드 바로 앞에 삽입하고, 첫 카드 위 경계에서만 호버 삽입선이 나타나며, 카드 사이마다 상시 `+`를 늘어놓지 않고, 목록 끝의 추가 행은 항상 보인다. 960px에서 행이 넘치면 데이터 열이 엘리전으로 먼저 물러나고 명령어·액션 각인은 마지막까지 온전하다. 방향·캐럿 글리프는 페인트로 그리고 텍스트 화살은 ASCII만 허용한다(`EAPO Mono`에 U+25B8가 없어 두부가 된다). 컨트롤은 그 스킨이 이미 가르친 '누를 수 있는 형태'(studio 유리판, minimal 각인 명령, soft 알약, rack 캡과 버튼, matrix 셀)를 입는다.

## 아이코노그래피

명령 픽토그램은 `Icons` 자산 묶음의 `eq-*`, `graphic-eq`, `preamp-gain`, `delay-clock`, `route-channels`, `channel-select`, `file-include`, `plugin`, `device-speaker`, `stage-chain`, `loudness`, `subwoofer-routing`, `multi-convolution`, `waveform`, `comment-bubble`, `logic-if`, `logic-eval` 열여덟 종이고, 24px 뷰박스에 1.8px 라운드 스트로크의 단일 잉크 선화다. 파일의 잉크는 검정으로 굳어 있어 Editor는 런타임에 스킨 잉크로 다시 칠하고, 이 시스템의 미리보기는 경로를 인라인해 `currentColor`로 잉크를 준다. 배지 안의 내용물은 이 픽토그램이고 영어 모노그램(`BQUAD`, `INC`, `VST`)은 미지 명령의 폴백으로만 남는다. 그 밖의 캐럿(`chevron-*`), 체크(`check-light`는 흰 잉크, `check-dark`는 검정 잉크), 창 캡션(`window-*`), 파일 대화상자 내비게이션(`nav-*`, `folder-*`, `view-*`), 편집 동작(`undo`, `redo`, `cut`, `copy`, `paste`, `trash`, `select-all`)도 같은 묶음이다. rack은 비트맵 에셋을 절대 들이지 않고 나사·결·명판·LED를 전부 페인트로 그린다. 앱 아이콘은 `Logos` 묶음의 `app-icon.png`(256px, `app-icon.ico`에서 뽑음)이다.

## 문장과 라벨

UI 문자열은 짧게 쓰고 동작을 서술한다. 한국어 라벨은 Windows 관용구보다 동작 서술을 우선해 undo/redo를 '수정 취소'와 '다시 수정'으로 쓴다('실행 취소'는 exe 실행으로 오독된다). 각인 문구는 번역하지 않고 진짜 입력 요소의 placeholder만 번역한다. 문장은 사실과 결과만 담아 짧게 잇고, 램프 옆에 같은 뜻의 단어를 붙이지 않는다. 이모지는 쓰지 않는다.

## 갤러리가 증거다

외형 변경은 `Editor --skin-gallery <outDir>`가 스킨×다크/라이트로 렌더한 PNG로 확인하고, 무간섭 주장은 SHA-256 바이트 비교로 증명한다. `ISkin`에 훅을 추가할 때 기본 구현은 기존 모습과 픽셀 단위로 같아야 한다. 행 안에 가로 스크롤바가 보이면 렌더 실패다(`viewport-gate` 960px). 스킨별 chrome은 위젯 생성 시점에 만들어지므로 스킨 전환은 행 재생성으로 반영되고 재칠만으로 바뀌리라 기대하지 않는다. DeviceSelector는 `SkinThemeData` 한 단위와 QSS 자원만으로 같은 스킨을 입는다.

## Not synced

이 시스템은 `115dkk/EqualizerAPO-XT`의 `main@2ba424e7`(2026-09-17)에서 손으로 옮겼다. 컴포넌트는 Qt 위젯이라 번들이 없고 미리보기는 정적 HTML 재현이다(read-only 경로). 옮기지 않은 것은 레거시 `.ico` 툴바 아이콘 열여섯 개와 Inkscape 시대의 `power_on/off.svg`, `invert/normalize/reset_response.svg`, 툴바 확장 화살(레거시 행 전용), 한글 폰트의 서브셋 밖 글리프, QSS 안의 위젯별 리터럴 색(예: studio 메뉴바 잉크 `#BCC7DA`), 그리고 Heritage(레거시 행) 프레젠테이션이다. 컴포넌트 미리보기는 Qt가 그리는 것을 손으로 옮긴 정적 재현이고, 2026-09-18에 로컬 빌드의 오프스크린 갤러리(`Editor --skin-gallery`, 1350장)와 `DeviceSelector --skin-shots`에 대조해 치수와 배치를 맞췄다. 카드 행의 헤더 문법(34x30 컨트롤 박스, 44x30 배지, 40px 헤더)은 `components/bundle.css`의 `.crow` 절이 정본이다. 미리보기가 없는 것은 서브우퍼 라우팅 대화상자(`srdialog_*`, 1360x810), Velvet·Hilbert·MultiConvolution·Delay·Preamp의 개별 행 본문, 픽커의 빈 검색 상태, 세그먼트 컨트롤, 타이틀바, 드롭다운 메뉴(`menu_normal`), 그리고 갤러리의 hover·disabled 변형 대부분이다. Heritage는 선택 가능한 스킨이 아니라 레거시 행 설정이 렌더되는 모습이고 헌법이 없어 이 시스템에도 없다.
