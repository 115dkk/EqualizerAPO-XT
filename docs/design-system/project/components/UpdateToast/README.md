# UpdateToast

업데이트 알림 배너다. 카드 면(들린 면 + 헤어라인 보더 + 좌측 액센트 킬)은 위젯이 토큰으로 직접 그리는 공유 chrome이라 스킨이 소유하지 않고, 각 스킨은 그 위의 활자와 dismiss 버튼만 답한다.

## 공유 계약

토스트 면은 `*-card-hover`(surfaceRaised), 보더는 `*-border`, 왼쪽 킬은 `*-accent`다. 메시지는 번역된 UI 데이터라 각인이 아니다(rack에서도 대문자·트래킹 금지). 닫기 버튼은 그 스킨의 타이틀바 캡션 버튼 문법을 따른다. 알림은 판독값이 아니라 소식이다.

## 스킨별 재질

- **studio:** 유리판 위의 점등 칩이다. 위젯이 그리는 면이 곧 칩이고 시트는 메시지 잉크의 크기·굵기와 dismiss만 다듬는다. 닫기는 상자 없는 `studio-muted` 잉크가 호버에 커서 아래 액센트 빛 고임으로 답하고 눌림은 그 빛이 한 단 진해진다.
- **minimal:** 시트는 활자만 답한다. 메시지는 `minimal-text`, 닫기 `x`는 각인 명령 글리프로 캐논을 따르되 호버의 한 스텝은 `minimal-border`(토스트 그라운드가 이미 card-hover라 그 위의 스텝)이고 눌림은 반전이다.
- **soft:** 토큰 자체가 파스텔이라 공유 카드가 그대로 `soft-accent` 소식 카드가 된다. 메시지는 본문 잉크의 친근한 무게, 동작은 ON 알약. 닫기는 조용한 둥근 면이 호버에서 웜 앰버 파스텔(`soft-warning`)로 데워질 뿐 빨간 X는 없다.
- **rack:** 앰버 뉴스 킬을 단 토큰 카드에 따뜻한 본문 잉크의 문장. 닫기 캡은 작은 기계 버튼(베젤)으로 호버에서 앰버로 덥혀지고 눌리면 침강한다. LCD로 만들지 않는다. 알림은 서비스 태그다.
- **matrix:** 게시 셀(면 + 1px 룰 + 액센트 킬)에 시트는 리마크 계급만 공급한다. 본문은 모노 판독 라인, 닫기 셀은 `matrix-danger` 프리라이트다(배급표에서 dismissal에 허락된 유일한 색, 타이틀바 닫기 셀 선례).

## 상태

닫기의 hover는 위와 같고 pressed는 studio 빛 한 단, minimal 반전 블록, soft 파스텔 한 스텝 깊게, rack 침강, matrix 체결이다. 토스트 자체에 hover·disabled는 없다.

## 소비자가 주는 것

메시지 문자열(이 미리보기의 문구는 소스에 없는 예시다), 동작 라벨, dismiss 핸들러. hand-written from `Editor/skins/<id>/qss/*_dark.qss` (UpdateToast rules) and the constitutions' 업데이트 토스트 sections.
