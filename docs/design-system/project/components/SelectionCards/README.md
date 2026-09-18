# SelectionCards

Device·Channel·Stage 카드의 본문, 곧 사용자가 켜고 끄는 선택지의 표현이다. 세 카드는 명령이 아니라 범위를 정하는 카드이고, 선택지 하나는 그 스킨이 가르친 '누를 수 있는 형태'(총강 조문 6)를 입는다. 미리보기는 갤러리 `device_normal`, `channel_normal`, `stage_normal`을 옮겼다.

## 공유 계약

Device는 마스터(`All devices`), 접힘 컨트롤(`Show all (+N)`), 장치 목록 순서이고 미설치 엔드포인트는 경고색 없이 '더 죽은' 형태다. Channel은 마스터 `ALL`, 채널 좌석, 그리고 조용한 `Add channel`이며 채널 이름은 데이터라 모노에 자간 1px이고 커스텀 채널은 점선이다. Stage는 어휘가 셋(`Pre-mix`, `Post-mix`, `Capture`)으로 고정된 유일한 카드라 두 레인(Playback/Recording)의 매치된 스위치 뱅크로 세우고 레인 캡션과 체인 화살표는 컨트롤이 아니라 길찾기다. 재생/캡처 구분은 형태의 축이 아니라 툴팁이 알린다.

## 스킨별 재질

- **studio:** 점등 유리 칩이다. 미선택은 불 꺼진 유리, 선택은 안에서부터 빛나는 반투명 액센트 채움(`studio-accent`), 호버·눌림은 광량 사다리의 한 단·최대 단이다. 마스터 칩은 한 단 뜨거운 광량으로만 서열을 만든다. Stage 칩은 왼쪽 3px 미니 램프를 단다.
- **minimal:** 1px 헤어라인 박스에 든 모노 토큰이다. 체결은 콘솔 reverse video(`minimal-text` 그라운드에 `minimal-surface` 잉크)이고 제3의 액센트 명도값은 없다. 헤더의 채널 스코프 토큰은 `minimal-ch-*` 맨몸 모노 대문자다. 레인 캡션은 각인 명령 레지스터.
- **soft:** 스타디움 알약이고 상태는 셋뿐이다. ON = `soft-accent` 파스텔 채움, OFF = 한 스텝 가라앉은 조용한 알약(`soft-graph` + 옅은 보더), 잠듦 = 삼중 조합. 마스터는 같은 고도에서 굵기와 더 깊은 파스텔로만 서열을 만들고 Stage는 같은 폭 알약의 매치된 뱅크다.
- **rack:** Device는 래칭 라우팅 스위치(재생 = 돌출 캡, 캡처 = 함몰 우물, 미설치 = 물러난 블랭크)이고 체결은 눌려 잠긴 캡(반전 베벨, 라벨 1px 하강, 앰버 백라이트)이다. Channel은 캡 아래 램프창이 켜지는 콘솔 어사인 키, Stage는 캡 상단 주얼 램프가 켜지는 INSERT POINT 스위치다. 마스터는 이 기계의 유일한 녹색(`rack-accent2`)으로 켠다.
- **matrix:** Device는 함몰 목적지 셀(산세리프, 이름은 값이 아니다), Channel은 좌표 셀(모노 700 + 자간, 24px 정사각 좌석), Stage는 투명 기능 셀(대문자 캡션, engage 문법)이다. 체결은 `matrix-accent` 틴트 + 액센트 룰이고 미설치는 결항 편이다.

## 상태

hover는 체결 깊이 프리뷰, focus는 각 스킨의 `*-focus`, 체결 + 비활성은 액센트를 남기지 않고 중립 명도 차(studio)나 뮤트된 반전(minimal), 녹색 기억(rack 마스터)으로만 기록을 남긴다.

## 소비자가 주는 것

장치·채널·스테이지 목록과 각 항목의 선택 여부, 설치·존재 여부, 마스터 상태, 접힌 항목 수. hand-written from `Editor/skins/<id>/qss/*_dark.qss` (device/channel/stage chip rules) against the gallery shots `device_normal`, `channel_normal`, `stage_normal`.
