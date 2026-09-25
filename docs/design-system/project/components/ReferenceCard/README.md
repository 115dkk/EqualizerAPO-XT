# ReferenceCard

Include·Convolution·MultiConvolution·VST 행의 본문, 곧 다른 파일이나 플러그인을 가리키는 참조 카드다. 각 스킨의 `cards/<Skin>ReferenceCardView.cpp`가 그리고, 미리보기는 갤러리의 `include_normal`(찾음)과 `include_missing_normal`(결손) 두 상태를 옮겼다.

## 공유 계약

위치는 언제나 컨테이너로 쓴다. as-written 접두(`Surround\`, 후행 구분자가 폴더임을 말한다)가 기본이고 폴더를 파일 뒤에 찍지 않는다. 정체성 사실(VST2/VST3, ABS)은 그 스킨의 기존 장식이 게시하고 별도 배지로 되풀이하지 않는다(총강 조문 2). 결손은 그 스킨의 상태 문법이 먼저 말하고 문장은 짧게 잇는다(조문 3). 복구 진입점은 `Locate`이고 그 스킨의 '누를 수 있는 형태'를 입는다(조문 6). 빈 참조는 결손이 아니라 미설정이다.

## 스킨별 재질

- **studio:** 이름은 판 위에서 가장 밝은 잉크로 서되 크기는 본문과 같다. 가라앉은 유리 데이터 창(`studio-surface-sunken`)이 위치 사실과 IR 판독값을 `EAPO Mono` muted 잉크로 담는다. 결손은 붉은 벽이 아니라 소등과 경고등이다. 이름이 muted로 어두워지고 `studio-danger`로 점등한 `MISSING` 칩 하나, 그리고 액센트 보더 유리를 입은 `Locate...`가 선다. Include 카드의 보더는 점선이고 램프를 달지 않는다.
- **minimal:** 문자 그대로 한 줄이다. 컨테이너 접두가 보조 잉크로 줄을 열고 페이로드가 `minimal-ink-bright`로 잇는다. 액션은 줄 끝의 대문자 모노 각인 명령(`BROWSE`/`OPEN`/`LOCATE`)이고 결손은 반전 블록 `MISSING`이다. 아이콘도 필도 없다.
- **soft:** 픽커의 타일 문법을 승격한 34px 둥근 사각 파스텔 타일이 행을 이끌고 두 줄 정체성이 따른다. 결손은 타일이 `soft-danger` 파스텔로 바뀌고 픽토그램 대신 스트로크 느낌표가 들어가며 `Locate...` 액센트 파스텔 필이 주인공이 된다. MISSING 배지도 빨간 벽도 없다.
- **rack:** 왼쪽 베젤 상태 램프(`rack-accent2`), 각인 라벨 스트립(`PATCH` 캡션 + 이름 + muted 위치), 함몰 LCD 판독창, 기계 버튼 열 순서다. 결손은 서비스 컨디션이다. 램프가 `rack-danger`로 점등하고 이름이 물러나며 앰버 `NOT FOUND` 각인과 `LOCATE` 캡이 붙는다. 캡션 각인은 9px, 이름은 14px다. 서비스가 읽을 수 없는 위치나 로더 오류처럼 문장이 필요한 상태는 라벨 스트립에 끼우지 않고 유닛 줄 아래 판 전폭에 한 줄을 따로 써서 길면 줄바꿈한다(VSTBus의 거부 상태와 같은 자리).
- **matrix:** 피드 라인이다. 함몰 모노 마커 셀(`> SRC`/`> IR`/`> IR+`), `<dir>@` 위치 판독, 가장 밝은 모노 페이로드 순서다. 결손은 마커 셀이 속 빈 `matrix-danger`의 `MISSING`으로 전환하고 `LOCATE` 모노 캡스 셀이 복구 진입점이다.

## 상태

hover·selected·focused·disabled는 카드 행의 문법을 따르고, 결손·미설정·검증됨은 위의 각 스킨 표기가 맡는다. VST의 로드된 ABI는 rack 황동 명판, matrix 포트 스트립 각인, studio 점등 칩, minimal 맨몸 토큰이 게시한다.

## 소비자가 주는 것

참조 종류, 이름, as-written 위치와 해석 경로, 존재 여부, IR 판독값(길이·샘플·샘플레이트·채널), 편집 가능 여부. hand-written from `Editor/skins/<id>/cards/<Skin>ReferenceCardView.cpp` against the gallery shots `include_normal` and `include_missing_normal`.
