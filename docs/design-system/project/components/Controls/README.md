# Controls

QSS가 입히는 기본 컨트롤(버튼, 입력창, 콤보, 값 스크럽, 체크박스)과 카드 헤더의 전원 토글이다. 값은 전부 각 스킨의 `*_dark.qss`/`*_light.qss`에서 왔고 라이트 시트에만 있는 리터럴은 토큰으로 근사했다.

## 공유 계약

파라미터 모드 콤보(`paramSelector`)는 진짜 선택기이므로 라벨로 위장하지 않고 선택기임이 항상 보여야 한다. 이름 콤보(`filterSelector`)는 행의 명령어라 본문 잉크를 쓴다. 값 박스(`valueScrub`)는 네이티브 스텝퍼 없이 세로 드래그·휠·타이핑으로 입력한다. 콤보와 스핀박스의 화살은 전 스킨이 `chevron-*` 아이콘 12px로 통일한다. 기본 버튼(`:default`)은 어느 스킨에서든 1px 액센트 보더다.

## 스킨별 재질

- **studio:** 버튼과 콤보는 `studio-card` 유리에 1px `studio-border`, 윗변 반사광 α 0.10, 라운드 8px. 입력창과 값 박스는 가라앉은 유리(`studio-input`, `studio-surface-sunken`)로 윗변 안쪽 그림자 α 0.55. 파라미터 선택기는 조용한 유리 스트립(투명, `studio-muted` 잉크, 보이는 화살), 이름 콤보는 raised 유리에 굵기 600. 체크는 `studio-accent` 채움, 전원은 점등된 유리 링.
- **minimal:** 버튼은 `minimal-card` + 1px 헤어라인 + 라운드 0에 대문자 자간 1px Bold(각인 명령 캐논). 선택기는 X5 문법(투명 + 1px 밑줄 + 항상 보이는 캐럿)으로 이름 콤보는 본문 잉크, 파라미터 콤보는 보조 잉크다. 값은 chrome 없이 `minimal-ink-bright` 굵은 모노로 스트립에 바로 인쇄한다. 체크 14px, 전원은 `ON`/`OFF` 각인.
- **soft:** 버튼 `soft-radius-control` 10px, 패딩 8/18. 입력창과 값 우물은 `soft-graph`(surfaceSunken)의 스타디움, 모드 박스는 본문 트레이 한 스텝 위(`soft-card-hover`)의 스타디움 필에 흐린 화살. 체크 18px, 켜지면 `soft-accent` 채움에 `soft-on-ink`. 전원은 파스텔 토글 알약.
- **rack:** 버튼과 콤보는 기계 가공 베젤(세로 그라데이션 #2C333A→#1B2126, 윗변 하이라이트 #3E474F, 라운드 3px, 각인 활자). 입력창은 `rack-input` 우물에 아랫변 밝은 립. 파라미터 선택기는 패널에 납작하게 각인한 캡션(`rack-muted` 굵은 소형 활자, 밀링 홈, 작은 삼각형). 값은 LCD 판독창(`rack-graph` + `rack-accent2` 세그먼트). 체크는 앰버 그라데이션(#FFD89A→#E8A33C) 램프, 전원은 앰버 LED가 박힌 로커.
- **matrix:** 버튼은 `matrix-card` 셀 + 1px 룰 + 라운드 0, 비활성은 점선 룰의 취소 셀. 입력창과 값 셀은 `matrix-graph` 함몰 셀. 파라미터 선택기는 muted 대문자 캡션의 투명 셀이되 1px 룰의 드롭다운 존과 화살은 유지한다. 이름 콤보는 함몰 판독 셀. 체크는 `matrix-accent` 체결, 전원은 액센트 LED 채움 셀.

## 상태

hover는 studio 보더 점등(α 0.45 액센트)과 유리 한 단, minimal 배경 한 스텝 + 액센트 보더, soft 한 스텝 상승, rack 앰버 보더, matrix 액센트 룰 + 프리라이트. focus는 각 스킨의 `*-focus`(studio 링, minimal 정사각 헤어라인, soft 할로, rack 서비스 엣지, matrix 셀 브래킷). disabled는 studio #5a5a72/#0d0d14, minimal #555555 + surface, soft muted α 0.55, rack #4E4A40, matrix #4A6470 + 점선 #1A2933(다크 리터럴, 라이트는 토큰 근사).

## 소비자가 주는 것

라벨 문자열, 값과 단위, 선택지 목록, checked/enabled 플래그. hand-written from `Editor/skins/<id>/qss/*_dark.qss` (QPushButton, QLineEdit, QComboBox, QComboBox[paramSelector], QComboBox[filterSelector], QAbstractSpinBox[valueScrub], QCheckBox::indicator).
