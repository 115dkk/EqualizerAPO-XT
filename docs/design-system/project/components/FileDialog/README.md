# FileDialog

Editor가 파일을 열고 저장할 때 띄우는 Qt 파일 대화상자에 각 스킨이 입힌 옷이다. 공용 `ISkin::styleFileDialog`가 내비게이션 행의 스트로크 아이콘 세트(`nav-back`, `nav-forward`, `folder-up`, `folder-new`, `view-list`, `view-detail`)를 깔고, 스킨은 잉크와 아이콘 제공자와 시트의 `QFileDialog` 범위 규칙으로 답한다. 미리보기는 갤러리 `filedialog_normal`(820x520)을 옮겼다.

## 공유 계약

구성은 제목 표시줄, `Look in:` 경로 콤보와 내비게이션 버튼 열, 왼쪽 사이드바, 파일 표(Name·Size·Type·Date Modified), `File name:` 입력과 `Files of type:` 콤보, Open·Cancel 버튼이다. 파일 이름 입력은 포커스를 받고 있으며 Open은 비어 있는 동안 비활성이다. 항목 픽토그램(폴더, 파일)은 스킨의 파일 아이콘 제공자(`SkinFileIcons`)가 그린다.

## 스킨별 재질

- **studio:** 대화상자 chrome은 툴바가 카드 뒤로 물러나듯 파일 데이터 뒤로 물러난다. 내비게이션 스트로크는 본문과 muted의 중간 잉크(반쯤 죽인 잉크)이고 입력창은 가라앉은 유리, 표는 유리판이다.
- **minimal:** 터미널의 파일 목록이다. 헤더는 대문자 자간, 전부 `EAPO Mono`, 항목 픽토그램은 헤어라인 제공자로 바뀐다. 포커스 입력은 액센트 헤어라인.
- **soft:** 툴바의 파스텔 타일이 대화상자의 내비게이션 행으로 이어진다. 이동 쌍(뒤로·앞으로)은 `soft-accent2`, 폴더 쌍은 열기의 웜 틴트와 새로 만들기의 액센트, 보기 토글은 muted로 남아 모드 스위치로 읽힌다. 항목 픽토그램은 soft의 파일 아이콘 제공자가 픽커·참조 카드의 둥근 파스텔 타일(모서리 32%) 위에 거의 흰 둥근 끝 스트로크로 그린다. 틴트는 두 가지뿐이라, 여는 곳(폴더, 드라이브, 컴퓨터)은 폴더 버튼의 웜 틴트(`soft-warning`의 파스텔)를, 파일은 Include 타일의 액센트(`soft-accent`의 파스텔)를 입고 파일 종류는 색이 아니라 글리프로 구분한다. 입력창은 스타디움, 표는 둥근 카드.
- **rack:** `OPEN FILE` 유닛이다. 내비게이션 버튼마다 태그를 달아 시트가 메인 툴바의 키처럼 기계 가공 캡으로 올린다(라운드 2 판정 "위쪽 툴바도 버튼처럼"). 경로와 입력은 우물, 표는 함몰 서브패널, 버튼은 각인 캡.
- **matrix:** 뷰 뒤에 흐린 보드 모눈이 깔리고 항목 픽토그램은 모따기된 CRT 글리프로 바뀐다. 입력·콤보·버튼·표 전부 1px 룰의 셀이고 헤더는 모노 대문자다.

## 상태

hover·pressed·focus는 각 스킨의 컨트롤 문법(Controls 카드)과 같다. 갤러리는 픽스처를 출력 디렉터리와 상관없이 `%TEMP%` 아래 이름이 고정된 폴더에 만들므로 이 장면은 실행마다 같다. 한 실행에서 처음 여는 대화상자만은 뒤로 버튼이 가끔 켜진 채로 찍혀, 갤러리가 그 장면을 `nondeterministic.txt`에 적는다.

## 소비자가 주는 것

현재 디렉터리와 항목 목록, 사이드바 즐겨찾기, 파일 형식 필터, 입력 중인 파일 이름. hand-written from `Editor/skins/<id>/<Skin>.FileDialog.cpp`, `Editor/skins/shared/SkinFileIcons.cpp` and the QFileDialog rules of each sheet, against the gallery shot `filedialog_normal`.
