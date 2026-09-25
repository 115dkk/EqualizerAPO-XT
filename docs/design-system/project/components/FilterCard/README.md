# FilterCard

명령 한 줄을 담는 카드 행의 껍데기(chrome)로, 다섯 스킨이 같은 헤더 순서 위에 각자의 재질을 입힌다. Qt 위젯(`FilterCardRow`)의 정적 HTML 재현이며, 원본은 `Editor/skins/<id>/<Skin>.CommandRows.cpp`와 각 스킨 QSS다.

## 공유 계약

헤더 컨트롤은 번호 바로 뒤의 고정 왼쪽 열에 서고 순서는 `[펼침][번호][전원][+][-][편집][종류 배지][제목][요약][채널 배지][여백]`이다. 요약은 구조화된 행에서 비어 있다(파라미터 에코 폐지). 헤더 `+`는 이 카드 바로 앞에 새 카드를 만든다. 원문(raw)은 카드에 인쇄하지 않고 헤더의 코드 마크(`</>`)가 연다. 960px에서 폭이 모자라면 데이터 열(위치, 이름)이 먼저 물러나고 명령어·액션 각인은 마지막까지 온전하다.

## 스킨별 재질

- **studio:** 알파 유리(`studio-card` 88%)에 윗변 1px 반사광, 왼쪽 가장자리의 18px 시그널 램프가 행의 밴드 컬러(`studio-band-*`)로 점등한다. 타입 배지는 같은 빛의 점등된 유리 칩, 채널 배지는 채워진 알약(ColorPill). 값 박스는 가라앉은 유리(`studio-surface-sunken`), 라운드는 `studio-radius` 하나.
- **minimal:** 1px 헤어라인 박스, 라운드 0, 헤더 판 없음. 행머리의 ASCII 글리프(`~` 필터, `>>` Include, `[]` VST, `->` Copy, `#` 주석)가 종류를 말하고 배지는 외곽선만 남는다. 수치는 chrome 없이 `minimal-ink-bright`로 바로 인쇄하고, 선택기는 캡션 + 1px 밑줄 + 캐럿이다. 채널은 `minimal-ch-*` 맨 잉크.
- **soft:** `soft-radius-card` 14px 카드, 한 스텝 어두운 받침(`soft-surface`)과 옅은 1px 보더로 고도를 만든다. 헤더 스트립 없음, 제목은 `title` 스타일(16px/600). 타입 칩은 softPastelize로 파스텔 선반에 올린 색 위에 `soft-on-ink`이고, 채널 알약은 파스텔로 바꾸지 않은 채널 식별 색이다. 값은 스타디움 우물.
- **rack:** 페이스플레이트(`rack-card` + 브러싱 결), 평상시 보더는 `rack-seam`, 랙 이어와 네 나사, paintLed 문법의 상태 LED(`rack-accent2`), 각인 라벨(`engraved`), 값은 LCD 판독창(`rack-graph` 우물 + 녹색 세그먼트 잉크). 이어 존은 콘텐츠 금지.
- **matrix:** 직각 셀 + 1px 룰, 왼쪽 `matrix-card-rail` 3px 신호등 레일과 5px 상태 램프, 좌표 셀(`coordinate` 스타일, `B3`), 헤더 밴드 뒤의 24px 모눈. 타입은 단색 코드 셀, 본문 밴드는 `matrix-bg`로 통째 채우고 값은 함몰 박스 셀.

## 상태

hover·selected·focused·disabled는 각 스킨의 문법으로만 말한다. studio는 광량, minimal은 배경 명도 스텝, soft는 고도 한 스텝, rack은 램프와 전원 내림, matrix는 크로스포인트 점등과 결항(점선 룰 + 속 빈 앰버 램프 + 취소선). 비활성에 경고색을 입히는 스킨은 없다.

## 소비자가 주는 것

행 번호(또는 matrix 좌표), 명령 타입(픽토그램과 `type-*` 색), 제목, 채널 목록, 본문 파라미터. 이 미리보기는 BiQuad(peaking) 행을 손으로 옮긴 정적 재현이다(hand-written from `Editor/widgets/FilterCardRow.cpp`).
