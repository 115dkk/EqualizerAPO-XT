# Toolbar

창 상단의 메뉴바 스트립과 메인 툴바(`QToolBar#MainToolBar`)다. 메뉴 항목의 열림, 툴버튼의 호버, 우측 상태 판독까지 각 스킨의 QSS와 `*Skin.Toolbar.cpp`가 답한다. 픽토그램은 `Icons` 묶음의 `file-new`, `folder-open`, `save`, `undo`, `redo`다.

## 공유 계약

툴바 높이는 `toolbar-height` 36px다. 열린 메뉴의 부모 항목은 Qt가 `:selected`로 유지하므로 점등 상태가 곧 열림 상태다. 상태 판독(저장됨·수정됨, 지연 시간)은 툴바 오른쪽 끝에 놓는다.

## 스킨별 재질

- **studio:** 메뉴바는 창의 유리를 잇는다. `studio-bg` 위에 #BCC7DA 잉크(시트 리터럴), 아래에 양끝에서 소멸하는 그라데이션 헤어라인. 열린 항목은 아래에서 차오르는 방사형 액센트 빛(α 0.34→0.14→0)이다. 툴버튼은 투명 8px 라운드이고 호버는 커서 아래에 고이는 액센트 빛이다.
- **minimal:** TUI 메뉴 라인이다. `minimal-surface` 스트립 위 대문자 자간 1px Bold, 아래 전폭 헤어라인, 호버는 배경 한 스텝, 열린 항목은 `minimal-card` 블록(눌림은 반전 블록). 툴버튼은 아이콘 없는 각인 명령(대문자 자간 Bold)이고 호버는 `minimal-card`.
- **soft:** 같은 차분한 헤더 면(`soft-surface`)의 넉넉한 스트립이고 아래에 헤어라인이 없다(여백이 구분자). 항목은 스타디움 알약(라운드 14px, 패딩 6/16)으로 호버는 한 스텝, 열림은 `soft-card-hover`. 툴버튼은 원형(라운드 17px)이고 호버는 `soft-card` + 옅은 보더.
- **rack:** 각인된 기능 열이다. 밀링 스트립(#1B2126→#11151A 그라데이션, 윗변 #262D33, 아랫변 #060809) 위에 `rack-muted`의 대문자 자간 각인(9pt Bold). 호버는 램프 예열(앰버 워시 α 0.05→0.18), 열림은 풀 워밍(#F2DDB4 잉크). 툴버튼은 기계 가공 베젤 버튼(라운드 2px)이고 호버는 앰버 보더다.
- **matrix:** 보드의 헤더 행이다. `matrix-surface`에 24px 모눈 타일(`matrix-grid-*.svg`)을 깔고 항목은 1px 룰의 셀(패딩 4/14, 마진 4)이며 열림은 액센트 α 0.10 프리라이트. 툴버튼은 1px 룰 셀(높이 24, 라운드 0)이고 호버는 같은 프리라이트. 우측에는 저장됨(`matrix-success`)·수정됨(`matrix-warning`) 램프.

## 상태

hover와 open/selected는 위와 같고, pressed는 studio 빛 한 단 진하게, minimal 반전 블록, soft 파스텔 한 스텝 깊게, rack 앰버 워시 α 0.40, matrix 체결이다. disabled 툴버튼은 각 스킨의 비활성 문법을 따른다.

## 소비자가 주는 것

메뉴 제목 목록과 열린 항목, 툴 동작 목록과 아이콘, 상태 판독 문자열. hand-written from `Editor/skins/<id>/qss/*_dark.qss` (QMenuBar, QMenuBar::item, QToolBar#MainToolBar, QToolButton) and `Editor/skins/<id>/<Skin>.Toolbar.cpp`.
