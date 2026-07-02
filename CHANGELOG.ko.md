# 변경 이력

[English](CHANGELOG.md) | **한국어**

TheFireKahuna의 equalizerAPO64 트리에서 포크된 뒤(마지막 업스트림 커밋 `7156020`, 2025-12-16) EqualizerAPO-XT에 들어간 주요 변경 사항입니다. 포크 작업은 2026-05-22에 시작됐습니다.

버전은 CI가 커밋 메시지의 Conventional Commits 타입을 읽어 자동으로 올리므로, 일부 번호는 건너뛰었습니다(1.7, 1.9, 1.12.1, 1.14, 1.16, 1.23, 1.25는 릴리스된 적이 없습니다). v1.10.1까지는 태그에 `-main.<run>` 접미사가 붙었고, v1.11.0부터는 깨끗한 `vX.Y.Z` 이름을 씁니다. 각 버전의 설치 파일은 [Releases 페이지](https://github.com/115dkk/EqualizerAPO-XT/releases)에 있습니다.

## Unreleased

- MultiConvolution이 Channel 명령에서 독립했습니다. 새 매핑 문법
  `MultiConvolution: L=0+1 R=2+3 brir.wav`는 각 대상 채널 자신의 신호를
  임펄스 응답 파일에서 나열한 채널(0부터 셈)과 컨볼루션해 그 채널에 다시
  합산하며, 여러 출력과 Copy식 가상 채널 대상을 한 줄로 처리합니다. 채널
  하나만 쓰는 기존 단순형(`MultiConvolution: L brir.wav`)은 그대로 유효하되
  이제 파일의 모든 채널을 뜻하고, 참여 범위는 파일 스스로가 정합니다.
  v2.5.0~v2.6.0의 동작이 바뀐 것입니다. 이전에는 앞선 `Channel:` 줄이 고른
  채널을 읽었는데, 그 패턴은 `Copy:` 보조 채널로 여전히 표현할 수 있으며
  새로 쓴 설정 레퍼런스에 크로스피드 레시피가 있습니다. ([#139])
- MultiConvolution 카드가 Copy와 같은 스킨별 라우팅 화면(studio 노드 그래프,
  minimal 스텝 리스트, soft 수식 블록, rack 패치베이 노브, matrix 크로스포인트
  격자)으로 매핑을 편집합니다. 입력 포트는 파일의 채널로 고정되고 게인
  팩터가 없어 패치 포인트는 연결 여부만 가집니다. 출력 포트는 그 행에서
  쓸 수 있는 채널과 카드에서 추가한 가상 채널이고, 읽을 수 있는 파일이
  없으면 편집 대신 이유를 보여줍니다. ([#139])

## v2.6.0 (2026-07-02)

- 파일 참조 행(Include·Convolution·MultiConvolution·VST 플러그인)을 스킨별
  참조 카드로 다시 만들었습니다. 다섯 스킨이 같은 사실(대상의 이름이 1차,
  위치는 포함 관계 그대로의 접두 `Surround\`, 깨진 참조는 상태 전환과
  Locate 복구 진입점, 임펄스 응답 판독값)을 각자의 구성으로 보여줍니다.
  studio는 가라앉은 유리 데이터 창 위에 정체성을 세우고, minimal은 터미널
  한 줄로 찍고, soft는 픽토그램 타일이 행을 이끌고, rack은 상태 램프·각인
  캡션·LCD 판독창을 단 서비스 유닛을 만들고, matrix는 VST에 포트 스트립이
  붙는 보드 피드 라인을 세웁니다. ([#137])
- 필터 카탈로그를 덮는 픽토그램 세트(공용 스트로크 문법 아이콘 18종)를
  추가했습니다. biquad 응답 곡선 8종, MultiConvolution 전용 겹층 마크,
  그리고 채널·주석·복사·지연·장치·그래픽 EQ·라우드니스·프리앰프·스테이지
  글리프입니다. Soft 스킨의 필터 픽커와 참조 타일이 두 글자 영문 모노그램
  대신 이 그림들을 보여줍니다. ([#137])
- rack 스킨이 커스텀 페이스플레이트 위젯(노브·램프·각인 라벨·LCD 우물)마다
  앱 배경색 사각형을 깔아, 브러시드 판에 새긴 부품이 아니라 붙여 놓은
  딱지처럼 보이던 문제를 고쳤습니다. 프리앰프 노브 주변이 최악이었습니다.
  노브 캡을 가로질러 포인터 선을 자르던 값 표시창도 제거했습니다. 값은
  노브 옆 LED 표시가 이미 보여줍니다. 이 결함은 이번 재작업 이전의 릴리스
  빌드에도 있었습니다. ([#137])
- 스킨 갤러리의 심사 세트가 460장으로 늘었습니다. 프리앰프 행, 중첩·결손
  Include 장면, 해석 가능한 Convolution 장면이 모든 스킨의 두 모드에서
  렌더됩니다. ([#137])

## v2.5.2 (2026-07-01)

- MultiConvolution 카드가 출력 채널을 자유 입력 상자 대신, 그 위치에 존재하는
  채널의 드롭다운에서 고르게 바뀌었습니다. 이 필터는 여러 입력을 한 채널(BRIR의
  한쪽 귀)로 합치므로 출력은 거의 항상 이미 쓰이는 실제 채널입니다. 그래서 카드가
  그 채널들을 제시하고, 가상·커스텀 이름은 여전히 직접 입력할 수 있습니다. 레거시
  행 편집기도 같은 선택기를 씁니다. ([#136])

## v2.5.1 (2026-07-01)

- 2.5.0에 망가진 채로 나간 MultiConvolution 필터의 Editor UI를 고쳤습니다. 삽입
  메뉴에서 기존 '고급 필터' 그룹에 들어가지 않고 번역이 없는 'Advanced filters'
  그룹으로 따로 떴습니다. 픽커는 필터를 번역된 카테고리 이름으로 묶는데, 새
  필터의 카테고리 문자열에 번역이 없어 영어로 남았고, Convolution과 라우드니스
  보정만 번역된 이름으로 묶였기 때문입니다. 독일어·프랑스어·한국어·중국어 간체
  카탈로그에 번역을 넣어 이제 셋이 하나의 '고급 필터' 그룹에 들어갑니다. 필터를
  삽입하면 빈 행이 남기도 했습니다. 갓 삽입한 `MultiConvolution:` 템플릿에는 아직
  채널과 경로가 없는데, Editor는 레거시 편집기가 그 줄을 맡아야 필터 카드를 만들고,
  엄격한 파서가 빈 줄을 거부했기 때문입니다. 이제 `Convolution`과 똑같이
  `MultiConvolution` 줄도 키워드로 알아보고, 카드 헤더도 일반 텍스트 배지 대신
  MultiConvolution 배지를 보여줍니다. ([#132])

## v2.5.0 (2026-07-01)

- BRIR(Binaural Room Impulse Response) 재생을 위한 MultiConvolution 필터를
  추가했습니다. 기존 Convolution 필터는 각 채널을 제자리에서 자신의 임펄스
  응답과만 컨볼루션합니다. 그래서 스테레오 IR을 걸어도 한쪽 입력을 반대쪽
  출력 채널로 보낼 수 없었고, BRIR에 필요한 크로스피드(한쪽 가상 스피커의
  소리가 반대쪽 귀에도 도달하는 성분)가 사라졌습니다. MultiConvolution은
  선택한 여러 입력 채널을 하나의 다채널 IR 파일에서 대응하는 채널과 각각
  컨볼루션한 뒤 그 결과를 합산해 하나의 출력 채널로 보내므로, 이 크로스피드가
  살아 있습니다. config 문법은 `MultiConvolution: <출력 채널> <다채널 IR
  경로>`이며, 첫 토큰이 출력 채널이고 나머지가 IR 경로입니다. 완전한 양 귀
  BRIR을 구성하려면 입력을 복제하는 Copy와 함께 이 필터 두 개(귀마다 하나씩)가
  있어야 합니다. Editor에는 현대적인 카드 에디터(출력 채널 입력란, 파일
  선택이 붙은 IR 경로 입력란, 5종 스킨 모두 지원)와 레거시 행 위젯을 함께
  추가했습니다. ([#130])

## v2.4.2 (2026-06-30)

- 이제 Editor가 텍스트를 Windows ClearType 대신 FreeType의 그레이스케일
  안티앨리어싱으로 그립니다. 번들된 한글 폰트(Pretendard)는 CFF/OpenType
  글꼴이라, ClearType가 서브픽셀 색 번짐을 넣어 보통 밀도의 모니터에서 흐릿하게
  보였습니다. 고해상도 화면(예: 4K 150%)은 픽셀이 촘촘해 이 번짐을 가렸고,
  그래서 해상도가 낮은 화면에서만 흐림이 드러났습니다. Editor를 Qt의 FreeType
  폰트 엔진으로 바꿔 색 번짐을 없애고, 모니터가 달라도 글자가 일관되게 나옵니다.
  장치 선택기와 업데이트 확인 도구는 그대로입니다. 시스템 폰트를 쓰는데
  ClearType에서 깔끔하게 나오기 때문입니다. 예전 ClearType 렌더링으로 되돌리려면
  환경 변수 QT_QPA_PLATFORM을 `windows`로 설정하면 됩니다. ([#129])

## v2.4.1 (2026-06-28)

- 2.3.0에 들어간 Editor 번역을 다듬었습니다. 채널 구성의 'From device' 항목이
  한국어·독일어·중국어에서 장치 설정을 따른다는 뜻으로 자연스러워졌고(전에는 말이
  잘린 느낌이었습니다), 프랑스어에 영어로 남아 있던 'VST plugin'을 번역했으며,
  독일어·프랑스어·중국어 용어 몇 개를 일관되게 고쳤습니다(라우드니스와 볼륨 구분,
  Copy 필터의 할당 개수, 파일 없음 문구). ([#128])

## v2.4.0 (2026-06-27)

- 이제 Editor 인터페이스 전체가 출시 4개 언어(한국어·독일어·프랑스어·중국어 간체)로
  번역됩니다. 이전 빌드는 메뉴 막대와 일부 대화상자만 번역돼 있어, 모던 카드 UI
  대부분과 필터 선택기, 필터별 편집기(채널·복사·장치·컨볼루션·포함·VST·그래픽 EQ·
  라우드니스), 가져오기 대화상자, 장치·스트림 형식 상태 메시지가 영어로 나왔습니다.
  이 문자열을 네 언어 카탈로그에 모두 채웠고, 필터 카드의 제목과 요약(`프리앰프`,
  `복사`, `%1개 밴드` 등)도 번역할 수 있게 했습니다. 단위 접미사와 숫자 형식, 스킨
  브랜드명은 일부러 영어로 남겼습니다. ([#126])

## v2.3.0 (2026-06-26)

- Convolution 필터 행에 현대적인 카드 에디터가 생겼습니다. 예전 인라인 위젯 대신
  다른 필터 카드와 같은 카드 방식으로 바뀌었습니다. 파일을 고르면 임펄스 응답의
  길이와 샘플레이트를 바로 보여주고, 그 샘플레이트가 재생 장치와 다르면 경고합니다.
  Include 행에 이미 있던 'config 디렉터리로 가져오기' 버튼도 추가됐습니다. 고른
  임펄스 응답이 config 폴더 밖에 있으면(오디오 서비스는 그 바깥 파일을 읽을 권한이
  없습니다) 버튼 한 번으로 config 폴더에 복사하고 경로를 그 사본으로 바꿔, 서비스가
  읽지 못해 조용히 실패하는 대신 컨볼루션이 제대로 동작합니다. ([#125])
- 콤보 박스와 스핀 박스(분석 바의 채널·위치 선택과 해상도 입력 등)에서 드롭다운과
  위/아래 화살표가 삼각형이 아니라 납작한 '-'로 나오던 문제를 고쳤습니다. 스킨이
  이 화살표를 CSS 보더 삼각형으로 그렸는데 Qt 6.10에서 막대로 무너졌습니다. 이제
  모든 스킨에서 안정적으로 그려지는 chevron 아이콘을 씁니다. ([#125])
- 스킨의 모노스페이스 영역에 나오는 한글이 이제 진짜 고정폭 CJK 폰트로 그려집니다.
  리디자인의 모노 폰트(DM Mono)에는 한글 글리프가 없어 한글이 비례폭인 Pretendard로
  떨어지면서 모노스페이스 정렬이 깨졌습니다. 한글과 ASCII로 서브셋한 Sarasa Mono
  K(OFL-1.1)를 번들하고 모노 폴백에서 Pretendard보다 앞에 둬, 모노스페이스 한글이
  격자에 맞습니다. ([#125])

## v2.2.1 (2026-06-21)

- ARM64에서 재생 중 설정을 다시 읽을 때 오디오 스레드가 아직 완성되지 않은 필터
  설정을 받을 수 있던 메모리 가시성 경합을 고쳤습니다. 로더가 새 설정을 실시간
  스레드에 release/acquire 플래그로 게시하도록 바꿔, 오디오 스레드가 구성이 보이기
  전에 그것을 읽지 않습니다. x86/x64 빌드는 영향이 없습니다(메모리 모델이 이미 이
  순서를 보장합니다). ([#124])

## v2.2.0 (2026-06-21)

- Windows 쪽 보안 감사에서 나온 항목 몇 가지를 고쳤습니다. 장치 선택기와 업데이트
  확인 도구가 Qt 플러그인을 작업 디렉터리 기준이 아니라 실행 파일이 있는 폴더에서
  불러오도록 바꿔, 권한이 높은 장치 선택기로 코드를 실행시킬 수 있던 DLL 검색 순서
  하이재킹을 막았습니다. 실시간 오디오 엔진에서는 잘못된 설정으로 오디오 서비스가
  죽지 않게 했습니다. 범위를 벗어난 `Delay:` 값은 상한을 두고 버퍼 할당 실패를
  확인하며(실패하면 소리를 지연 없이 그대로 통과), 프레임이 없거나 채널이 없거나
  길이가 비정상인 `Convolution:` 임펄스 응답은 처리 전에 거부해 빈 버퍼를
  참조하거나 무한 루프에 빠지지 않게 했습니다. ([#123])

## v2.1.0 (2026-06-20)

- Device 필터 행에서 장치를 카드 안에서 바로 고릅니다. `Device:` 줄이 어떤 재생·
  녹음 장치에 적용될지 정할 때 더는 별도 다이얼로그를 열지 않습니다. 카드 본문에
  장치마다 체크 가능한 칩과 'All devices' 칩이 나오고, APO가 설치되지 않은 장치는
  'Show all' 토글 뒤에 숨깁니다. 칩은 네이티브 Windows 장치 트리 대신 카드의 나머지
  요소처럼 각 스킨 스타일로 그려지며, 기록되는 줄은 기존 변경 버튼 다이얼로그가
  만들던 것과 바이트 단위로 같습니다(`EditorLogicTests`의 회귀 테스트가 이
  직렬화를 고정합니다). ([#120])

## v2.0.1 (2026-06-19)

- 일부 저장된 창 레이아웃에서 Editor가 시작되지 않던 크래시(액세스 위반)를
  고쳤습니다. `loadPreferences()`가 `QMainWindow::restoreState()` 앞뒤로 분석
  dock을 `removeDockWidget()` + `addDockWidget()`으로 다시 배치했는데,
  `restoreState()`가 막 배치한 dock을 제거하면서 dock 영역이 아직 참조하던 레이아웃
  아이템을 해제했고, 첫 창 표시에서 그 매달린 아이템을 역참조해(use-after-free)
  창이 뜨지 않았습니다. 무겁거나 오래된 저장 레이아웃에서 #54/#75 시작 크래시가
  재발한 것입니다. 이제 분석 dock은 목표 위치에 있지 않을 때만 다시 배치하므로,
  불필요한 재배치가 사라지고 저장된 레이아웃은 초기화되지 않습니다. ([#118])

## v2.0.0 (2026-06-18)

- 고DPI(배율) 디스플레이에서 VST3 플러그인 에디터 창 크기가 어긋나던 문제를
  고쳤습니다. 에디터가 플러그인의 물리 픽셀 크기를 논리(장치 독립) 픽셀 단위로
  재는 Qt 위젯에 그대로 넘겨, 150%나 200% 모니터에서는 호스트 프레임이 너무 크게
  잡히고 플러그인(예: FabFilter Pro-Q)이 어긋난 캔버스에 그려졌습니다. 그래서
  게인 0 dB의 평평한 EQ 곡선이 왜곡돼 보이고 패널에 빈 여백이 생겼습니다. 이제
  임베드 패널, '패널 열기' 다이얼로그, 카드 임베드 모두 프레임의 device pixel
  ratio로 플러그인의 물리 크기를 논리 픽셀로 바꾸고, 네이티브 호스트 창은 물리
  픽셀로 유지하며, DPI를 인식하는 플러그인에는 `IPlugViewContentScaleSupport`로
  호스트 배율을 알려줍니다. 100%에서는 동작이 그대로입니다. ([#108])
- 제거 시 모든 오디오 장치가 사라져 재부팅해야 복구되던 치명적 버그를
  고쳤습니다. 제거 과정이 Windows Audio 서비스만 재시작해, Windows Audio
  Endpoint Builder가 방금 제거된 APO를 가리키는 낡은 엔드포인트 그래프를 그대로
  들고 있었습니다. 그래서 사용 중이던 장치가 재부팅으로 그래프를 다시 만들기
  전까지 쓸 수 없었습니다(레지스트리는 이미 깨끗했고, 원래 드라이버 효과가
  복원돼 있었습니다). 이제 제거 시 Windows Audio Endpoint Builder를 재시작해
  라이브 그래프를 재구성하므로 오디오 장치가 사라지지 않습니다. dispatch 전용 CI
  워크플로우가 오디오를 재생 중인 실제 가상 엔드포인트에서 이를 재현하고 수정을
  검증합니다. ([#105])
- 현재 Windows에서 시스템 효과가 실제로는 한 번도 로드되지 않아 EQ가 조용히 꺼져
  있던 문제와, 일부 장치에서 Device Selector 설치 테스트가 `Initialize failed for
  device "..." (매개변수가 틀립니다)`로 실패하던 문제를 고쳤습니다. APO가
  `IAudioSystemEffects2`를 노출하기 시작하면서 오디오 엔진이 `Initialize`에 더 큰
  `APOInitSystemEffects2` 구조체를 넘기는데, APO는 옛 `APOInitSystemEffects` 크기만
  정확히 받아들여 모든 초기화를 `E_INVALIDARG`로 거부하고 로드 전에 빠져나왔습니다.
  이제 더 큰 구조체를 받아들이므로(모든 `APOInitSystemEffects` 버전이 앞부분 필드를
  공유) 효과가 다시 로드되어 오디오를 처리하고, 장치 테스트도 통과합니다. ([#107])
- minimal·soft·rack 스킨으로 바꿀 때, 그리고 일반 재적용이나 다크 모드 토글에서
  간헐적으로 Editor가 죽던 문제를 고쳤습니다. 스킨 전환은 속도를 위해 전역
  스타일시트를 바꾸기 전에 필터 행을 먼저 철거하는데, 스타일시트가 유발한 재배치가
  그리드 레이아웃이 잠깐 사라진 사이에 필터 테이블의 크기 힌트 갱신을 다시 호출해 널
  포인터를 역참조했습니다. 이제 행을 다시 만드는 동안에는 갱신이 아무 일도 하지
  않습니다. ([#107])

## v1.27.1 (2026-06-13)

- 시작 시 드물게 나던 크래시를 고쳤습니다. 구버전에서 저장한 설정을 열 때,
  현재 창 구조(커스텀 타이틀바가 메뉴바를 옮기면서 바뀜)와 맞지 않는 옛 창
  레이아웃을 복원하다 첫 그리기 단계에서 죽었습니다. 이제 저장된 레이아웃에
  버전을 붙여, 맞지 않으면 무시하고 기본 레이아웃으로 창을 한 번 엽니다. 설치
  폴더가 아닌 작업 디렉터리에서 띄울 때 Qt 플랫폼 플러그인을 못 찾아 "no Qt
  platform plugin could be initialized"로 실패하던 문제도 함께 고쳐, 플러그인
  경로를 실행 파일 폴더 기준으로 잡습니다. ([#98])

## v1.27.0 (2026-06-13)

- 적대적 디자인 리뷰 라운드 1: 파라미터 영역이 스킨의 소유가 됐습니다. 명령
  행의 네이티브 스핀 화살표가 사라지고(값은 드래그 스크럽), gain 노브는
  0 dB 디텐트의 바이폴라로 읽히며, 다섯 스킨이 행과 픽커를 다시
  만들었습니다. studio는 필터 타입별 밴드 컬러, minimal은 값-우선 헤어라인
  노브와 페이지 순 픽커 번호, soft는 웜 그라파이트 다크 정체성, rack은 각인
  캡션과 LCD 값 창, matrix는 버스 좌표와 스펙 에코 캡션 스트립입니다.
  오프스크린 갤러리는 250장이 됐고 행 가로 오버플로 시 렌더가
  실패합니다. ([#94])

## v1.26.0 (2026-06-12)

- Editor가 창 chrome을 직접 그립니다. 네이티브 윈도우 제목 표시줄이 스킨별
  타이틀바로 바뀌었고(드래그·스냅·모서리 리사이즈·더블클릭 최대화는 네이티브
  그대로), 메뉴바와 모든 드롭다운 메뉴가 스킨의 디자인 언어를 따릅니다.
  Studio Glass는 발광 구분선의 유리 패널, Precision Minimal은 아이콘 없는
  모노 메뉴의 터미널 타이틀 라인, Soft Lab은 차분한 둥근 헤더와 메뉴 카드,
  Hardware Rack은 LED 체크가 달린 각인 금속 패널, Signal Matrix는 셀 메뉴의
  모눈 마스트헤드입니다. Edit 메뉴에 남아 있던 2005년풍 아이콘도 모던
  스트로크 아이콘으로 교체했습니다. Interface 메뉴의 "Native title bar"
  토글로 재시작 후 순정 캡션으로 돌아갈 수 있습니다. 오프스크린 갤러리가
  스킨별 타이틀바·메뉴바·열린 메뉴를 한글 샘플과 함께 캡처해, 현장에서
  보고된 한글 씹힘을 상시 감시합니다. ([#88])

## v1.24.0 (2026-06-12)

- 메인 툴바가 윈도우 기본 모습과 2005년풍 아이콘에서 벗어났습니다. 새
  `ISkin::styleMainToolbar` 훅으로 스킨마다 다르게 입습니다. Studio Glass는
  테두리 없는 버튼 아래 빛이 고이는 유리 윗변, Precision Minimal은
  NEW/OPEN/SAVE를 모노 텍스트 명령으로 쓰는 터미널 명령줄, Soft Lab은 파스텔
  타일과 진짜 스타디움 토글의 차분한 헤더 밴드, Hardware Rack은 트랜스포트
  버튼·나사·LCD 저장 상태 창이 달린 브러시드 마스터 레일, Signal Matrix는
  상태 램프가 붙은 정사각 기능 셀의 보드 헤더입니다. 저장 상태 배지는 이제
  하드코딩된 알약 대신 각 스킨이 직접 스타일하며, 오프스크린 갤러리가 모든
  툴바를 캡처합니다. ([#85])

## v1.22.0 (2026-06-12)

- 필터 추가 픽커가 모든 템플릿을 한꺼번에 쏟아내는 평면 목록에서, 추가
  버튼에 붙는 콤팩트한 드롭다운으로 바뀌었습니다. 카탈로그 표현은 스킨마다
  다릅니다. Studio Glass는 프로스트 글래스 패널, Precision Minimal은 숫자
  점프가 되는 번호식 터미널 인덱스, Soft Lab은 둥근 설정 메뉴, Hardware
  Rack은 LCD 검색창이 달린 1U 모듈 프리셋 브라우저, Signal Matrix는 2축
  크로스포인트 계기판입니다. 스킨은 새 `ISkin::createFilterPicker` 훅으로
  자기 픽커를 제공하며, 오프스크린 갤러리가 모든 픽커를 캡처합니다. ([#81])

## v1.21.0 (2026-06-12)

- 게인 노브(Preamp 카드 노브, biquad 게인 다이얼)가 설정 가능한 ±범위를
  돌도록 바뀌었습니다. 기본값은 ±20dB이고 View > Interface > Knob gain
  range에서 바꿀 수 있습니다. 직접 입력하는 값은 기존 전체 범위를 그대로
  받고 노브만 끝에 걸립니다. 기존에는 Preamp 노브가 ±100dB 고정이라 조금만
  돌려도 수십 dB씩 튀었습니다. ([#78])
- 분석 패널의 기본 높이를 줄였고, 원본 Equalizer APO처럼 아래쪽이 기본
  위치가 됐으며, 위치는 Ctrl+Alt+G 순환 대신 패널 컨트롤 바의 Pos
  드롭다운(위/아래/오른쪽)에서 직접 고릅니다. ([#78])
- Copy 명령을 일부러 비워도 모든 스킨에서 GUI로 다시 채울 수 있습니다.
  크로스포인트 격자(Signal Matrix)와 패치베이(Hardware Rack)는 장치 채널
  전체를 항상 표시하고, 스텝 목록(Precision Minimal)과 수식 블록(Soft Lab)은
  장치 채널마다 행을 만들고 행별 [+] 메뉴로 소스를 추가할 수 있으며, 계수를
  지우면 해당 소스가 빠집니다. 비어 있는 행은 설정 줄에 아무것도 쓰지
  않습니다. ([#78])

## v1.20.0 (2026-06-12)

- Editor가 예기치 않게 죽을 때 흔적 없이 사라지는 대신, 크래시 미니덤프와
  요약 리포트(버전, 예외 주소, 마지막으로 전환한 스킨)를
  `%LOCALAPPDATA%\EqualizerAPO-XT\crashdumps`에 남깁니다. 특정 머신에서만
  일부 스킨 선택 시 죽는 문제([#75])를 추적하기 위한 것으로, CI도 릴리스
  바이너리별 디버그 심벌을 보존해 덤프를 분석할 수 있게 했습니다. ([#76])

## v1.19.0 (2026-06-12)

- Editor의 5개 스킨이 색만 다른 변형이 아니라 서로 다른 시각 정체성이
  됐습니다. 명령 종류 표시, 호버, 비활성 상태, Include/VST 표현, 모서리
  언어, 위계를 스킨마다 고유한 형태·질감·타이포그래피로 답합니다. 유리
  카드와 발광 아크 노브(studio), 라운드 0의 헤어라인 터미널(minimal),
  여백이 넉넉한 둥근 설정 화면(soft), 나사·명판·포인터 노브까지 그려 넣은
  랙 하드웨어(rack), LED 링 인코더와 크로스포인트 호버를 갖춘 격자
  계기판(matrix)입니다. 격리된 구현 에이전트 5개가 만들고 차별화 심사를
  거쳐 통합했으며, 전체 기록은 docs/skin-integration-report.md에
  있습니다. ([#73])

## v1.18.0 (2026-06-12)

- 모던 카드의 Channel 행에서 채널 선택을 카드 안에서 바로 편집할 수 있습니다.
  장치 채널 칩, ALL 칩, 커스텀/가상 채널 칩과 이름 추가 입력란이 제공되며,
  원문 편집기나 레거시 대화 상자를 거칠 필요가 없습니다. 같은 선택은 기존
  대화 상자가 쓰던 것과 바이트 단위로 동일하게 기록됩니다. ([#70])
- 드롭다운이 너무 작게 그려지던 문제를 고쳤습니다. 모든 스킨이 공용 크기
  바닥값을 공유하고, 툴바 드롭다운은 시스템 글꼴 크기를 따라가며, 팝업
  목록은 가장 긴 항목에 맞춰 넓어집니다. ([#70])
- 5종 스킨 전면 개편(이슈 #66~#68)을 위한 Phase 0 기반 작업이 들어갔습니다.
  노브 페인팅과 명령 행 chrome이 `ISkin` 훅으로 위임되며(기본 구현은 픽셀
  단위로 동일함을 검증), 헤드리스 스크린샷 갤러리(`Editor --skin-gallery`)가
  스킨별 대표 행을 렌더링합니다. CI는 그 이미지를 `skin-gallery` 아티팩트로
  올립니다. ([#70])

## v1.17.2 (2026-06-12)

- 쌓여 있던 cppcheck 발견을 전수 분류했습니다. 실제 결함으로는 Include GUI가
  쓰는 파일 접근 검사에서 예외 경로마다 자원이 새던 문제, ARM64 네이티브 VST
  라이브러리를 아키텍처가 다르다고 잘못 보고하던 문제, Benchmark가 무음 출력에
  `log10(0)`을 호출하던 문제를 고쳤습니다. 그 외에는 멤버 기본값, const 참조
  전달, 명시적 `wstring::npos` 비교 같은 동작 동일한 정리를 트리 전체에
  적용했고, CI는 이제 버전을 고정한 cppcheck 2.21.0을 베이스라인 0 기준의
  차단 게이트로 돌립니다. ([#64])
- 남아 있던 필터 설정 문법(Stage, Include, Device, If/ElseIf/Else/EndIf,
  Eval과 인라인 백틱 식, IIR, LoudnessCorrection)을 엔진과 Editor가 함께 쓰는
  공용 명령 코덱으로 옮겨, #57에서 시작한 이전 작업을 마무리했습니다. 코덱마다
  왕복 테스트가 있으며, 쓰임이 없어진 ParameterArchive 헬퍼는
  제거했습니다. ([#63])
- `Channel:`과 `Convolution:` 설정 문법을 엔진과 Editor가 함께 쓰는 공용 명령
  코덱으로 옮기고 왕복(round-trip) 테스트를 붙였습니다. 이 과정에서 Channel
  GUI가 쉼표로 구분한 선택자를 무시하던 문제도 함께 고쳤습니다. `IFilterFactory`의
  소비 계약과, Editor/UpdateChecker 업데이트 경로를 의도적으로 분리해 둔 사실을
  코드에 주석으로 문서화했습니다. ([#57])
- 격주 감사가 Git Bash를 쓰는 Windows 러너에서, 미리 준비된 빌드 가능한 트리로
  실행됩니다. 이제 감사가 코드를 읽기만 하는 대신 직접 컴파일하고 테스트를
  돌릴 수 있습니다. ([#58])
- 버전이 오르지 않는 `main` push는 새 릴리스를 만들 수 없으므로 빌드 매트릭스를
  건너뜁니다. 전체 매트릭스 빌드는 workflow_dispatch로 언제든 직접 실행할 수
  있습니다. ([#61])
- README를 현재 상태에 맞게 새로 썼고, 이 변경 이력 문서를 추가했으며, 두 문서의
  한국어판을 만들었습니다. ([#60], [#62])

## v1.17.1 (2026-06-11)

격주 감사 이슈 #53에서 나온 첫 수정 묶음입니다.

- 자동 감지 설치기가 내려받은 설치 파일을 실행하기 전에 릴리스 자산
  `SHA256SUMS.txt`로 검증합니다. CI는 릴리스마다 체크섬 파일을 함께
  게시합니다. ([#56])
- libHybridConv의 버퍼 소유권을 보호 장치 없는 프로세스 전역 map에서 컨볼루션
  구조체 내부로 옮겨, APO 인스턴스 간에 잠재해 있던 데이터 경쟁을 없앴습니다.
  오디오 출력은 비트 단위로 동일합니다. ([#56])
- 새 테스트 스위트 `EngineOrchestrationTests`가 채널 이름 라우팅, `Copy` 명령의
  동작, 설정 교체 시 crossfade를 공개 엔진 API로 검증합니다. ([#56])
- CI 빌드 매트릭스를 `.github/simd-variants.psd1` 매니페스트에서 생성하고,
  바이너리 의존성 다운로드를 태그와 SHA-256으로 고정해 검증하며, 설치기 채널
  이름이 매니페스트와 어긋나면 lint 단계가 CI를 실패시킵니다. PR은 이제 정말로
  기본 avx2 변형만 빌드합니다(옛 PR 필터는 동작하지 않아 여섯 개를 모두
  빌드하고 있었습니다). ([#55])
- 격주 감사가 Claude Fable 5로 실행됩니다. ([#54])

## v1.17.0 (2026-06-10)

- **자동 감지 설치기**: 새 진입용 설치 파일 `EqualizerAPO-XT-Setup.exe`가
  CPU(아키텍처와 AVX 수준)를 감지해 맞는 빌드를 내려받습니다. 사용자가 SIMD
  변형을 직접 고를 필요가 없어졌습니다. ARM64 업데이트 채널 이름을 실제로
  게시되는 `arm64-neon`과 맞췄습니다. ([#52])
- 감사 이슈 #48의 두 번째 수정 묶음입니다. 공용 테스트 하니스를 만들어
  회귀/헬퍼/파서 커버리지를 늘리고, 자체 빌드 테스트 플러그인으로 VST2 호스트를
  런타임에 검증하는 테스트를 더했습니다. 비대해진 `VSTPluginInstance` 파일을
  응집된 단위로 나누고, BiQuad 설정 줄의 파싱 루틴을 하나로 합쳤으며,
  MainWindow에서 설정 파일 코덱을 분리해 냈습니다. Preamp·Delay·GraphicEQ·
  Copy·VSTPlugin GUI의 파싱/직렬화는 공용 코덱으로 묶었고, SIMD 변형 집합은
  `simd-variants.psd1` 한 곳에 정의했으며, SIMD 플래그 없이 qmake를 실행하면
  조용히 잘못 빌드되는 대신 즉시 실패합니다. ([#51])

## v1.15.3 (2026-06-09)

감사 이슈 #48의 첫 수정 묶음입니다. ([#50])

- 컨볼루션 IR 캐시에 크기 상한을 두고 `ConvolutionFilter`의 자원 처리를
  강화했습니다.
- 필터 factory가 중앙 레지스트리를 통해 등록되고, 할당은 타입을 검사하는
  할당자를 거치며, 인식된 명령이 필터를 만들지 못하면 엔진이 경고를 남깁니다.
  VST 플러그인 초기화 실패는 원인과 함께 기록됩니다.
- Editor의 알려진 명령 목록을 손으로 관리하던 사본 대신 factory 레지스트리에서
  생성하고, 레거시 필터 목록 UI 경로는 동결해 문서화했습니다. 릴리스 노트의
  SIMD 채널 표는 한 곳에서 생성합니다.

## v1.15.2 (2026-06-09)

- 감사 워크플로우의 actions를 Node 24로 올렸습니다. ([#49])

## v1.15.1 (2026-06-08)

- 격주 자동 코드 감사 워크플로우를 추가했습니다. Claude가 코드베이스를 점검하고
  발견 사항을 GitHub 이슈로 올립니다. ([#46], [#47])
- 런타임 SIMD dispatch(B안) 장기 계획을 `docs/RuntimeDispatchEpic.md`에
  기록했습니다. ([#45])

## v1.15.0 (2026-06-08)

- **Google Highway 포터블 SIMD**: 손으로 작성한 SIMD 네 곳(컨볼루션 커널,
  BiQuadFilter, PreampFilter, float↔double 변환)을 ISA별 intrinsic에서 변형별로
  컴파일되는 Highway 커널 한 벌로 이식했습니다. ARM64는 스칼라 폴백에서 실제
  NEON으로 올라갑니다. 새 회귀 케이스 `convolution_short`와 커밋된 참조
  데이터가 이식을 검증하며, 모든 변형에서 출력이 회귀 허용 오차 안에 있습니다.
  ([#43], [#44])
- Qt 응용 프로그램 실행 파일이 빠졌는데도 불완전한 릴리스를 패키징하는 대신,
  CI가 빌드를 실패시킵니다. ([#44])

## v1.13.1 (2026-06-08)

- VST 플러그인 라이브러리의 로드/언로드가 스레드 안전해졌습니다. ([#42])
- 사용자 문서를 영어와 한국어로 새로 썼고, CI가 GitHub Wiki에 게시합니다.
  ([#36], [#37], [#38], [#39])
- 오디오 회귀 참조 데이터를 커밋했고, 변형 간 비교가 출력이 없을 때 조용히
  통과하는 대신 실패합니다. ([#40], [#41])

## v1.13.0 (2026-06-07)

- **네이티브 VST3 호스팅**: Steinberg VST3 SDK pluginterfaces를 통해 VST3
  플러그인을 호스팅하며, 플러그인이 지원하면 double 정밀도로 처리합니다.
  ([#35])

## v1.12.x (2026-06-03 ~ 2026-06-06)

- 스킨과 테마를 전환할 때 위젯 트리 전체를 다시 polish하지 않아 전환이 거의
  즉시 끝납니다. (v1.12.5, [#34])
- 모던 필터 카드 아이콘이 제대로 그려지고 Copy 게인 라벨이 겹치지 않습니다.
  (v1.12.4, [#33])
- 필터 노브가 커서를 따라가는 진짜 회전 컨트롤이 됐고, 릴리스 파이프라인은 CI
  concurrency 그룹으로 직렬화됩니다. (v1.12.3, [#32])
- CI를 `windows-2025-vs2026` 러너 이미지로 옮기고 러너별 플랫폼 도구 집합을
  선택하며, GitHub Actions를 지원 종료된 Node.js 20에서 벗어나게 했습니다.
  `velopack_libc.dll`을 앱이 실제로 찾는 이름으로 배포해, 패키징된 빌드가
  시작되지 않던 문제를 고쳤습니다. (v1.12.2, [#29], [#30], [#31])
- 레거시 필터 카드가 모던 AudioKnob을 쓰고, 채널 선택 배지에 색이 입혀지며,
  Qt 고해상도 스케일링이 켜져 4K 화면에서 UI가 작게 나오지 않습니다.
  (v1.12.0, [#28])

## v1.11.0 (2026-06-03)

- **자동 업데이트**: Editor가 Velopack SDK로 새 릴리스를 백그라운드에서
  내려받아 두었다가 종료할 때 적용합니다. ([#27])
- 폰트 weight가 제대로 그려지고, 스킨을 바꾸면 Copy 라우팅 렌더러도 함께
  바뀝니다. ([#27])

## v1.10.x (2026-06-02)

- APO가 `IAudioSystemEffects2::GetEffectsList`로 자신의 효과를 보고해, Windows가
  어떤 처리가 켜져 있는지 표시할 수 있습니다. (v1.10.0, [#25])
- Copy 라우팅 편집기가 빈 캔버스 대신 장치의 채널 목록으로 시작합니다.
  (v1.10.1, [#26])

## v1.8.0 (2026-05-31 ~ 2026-06-02)

- **스킨별 Copy 라우팅 렌더러**: Editor의 다섯 스킨이 각자의 시각 언어로 채널
  라우팅을 그리며, 새 `ISkin` 위임 엔진이 이를 담당합니다. DM Sans, DM Mono,
  Pretendard 폰트를 내장하고 QSS 폰트를 토큰화했습니다. ([#22])
- FFTW planner 접근을 직렬화해 Editor 시작 시 간헐적으로 나던 크래시를
  고쳤습니다. ([#22])
- 비실시간 COM/Win32 자원을 RAII로 감싸고, APO가 regsvr32를 호출하는 대신
  프로세스 안에서 직접 등록합니다. ([#23])

## v1.6.0 (2026-05-26 ~ 2026-05-27)

- 샘플 형식이 IEEE_FLOAT 32/64가 아닐 때 APO가 오디오를 깨뜨리는 대신 그대로
  통과시키고, EQ가 꺼져 있다는 사실을 Editor가 표시해 사용자가 알 수 있습니다.
  ([#21])
- 변형 진단으로 찾은 Modern Card 렌더링 버그 3건을 고쳤고, Modern Card 오른쪽
  헤더 도구 모음이 다시 보입니다. ([#19], [#20])

## v1.5.x (2026-05-24 ~ 2026-05-25)

- **Editor 스킨 5종**(studio, minimal, soft, rack, matrix)이 스킨별 토큰 QSS로
  뚜렷이 구분되는 모습을 갖췄고, Conventional Commits 기반 자동 버전 bump가
  들어왔습니다. (v1.5.0, [#11], [#12])
- 모든 필터 factory가 Common.lib에서 링크됩니다(이전에는 일부 필터가 조용히
  동작하지 않았습니다). FFTW wisdom을 캐시합니다. (v1.5.1, [#13])
- DSP 핫패스, Editor 분석 패널, 컨볼루션에 성능 패스를 돌렸습니다. 디코딩한
  임펄스 응답을 필터 타입별로 캐시하고, 켜기/끄기 시 해당 행만 새로 그리며,
  Velopack 업데이트 확인을 시작 후 60초로 미뤘습니다. (v1.5.1, [#14], [#15],
  [#16], [#17])
- 주석 처리된 행이 카드 전체를 회색으로 만들지 않고, BiQuad 카드 요약이 더
  풍부해졌습니다. (v1.5.2, [#18])

## v1.4.3 (2026-05-22 ~ 2026-05-24)

- **Velopack 이행(1~5단계)**: 헤드리스 APO 등록, Editor의 Velopack
  설치/업데이트/제거 훅, 원시 바이너리를 Velopack에 직접 패키징, 백그라운드
  업데이트를 트리거하는 런타임 헬퍼, 그리고 NSIS 설치기와 작업 스케줄러 기반
  업데이트 경로 제거까지 끝냈습니다. ([#4])
- **AudioRegressionTests**: DSP 시나리오를 렌더링해 커밋된 참조 데이터와
  비교하는 회귀 스위트를 만들고, CI에서 변형 간 비교, cppcheck과 함께
  돌립니다. PR 빌드와 push 빌드를 분리했습니다. ([#6])
- SSE2와 AVX 릴리스 채널을 추가하고(SIMD 수준이 낮은 빌드는 스레드 FFTW 사용),
  AVX용
  Velopack 피드 자산을 인식하며, 레거시 카드 편집기를 교체하고, Editor UI에서
  Qt 기본 스타일을 걷어냈습니다([#3]).
- Benchmark에 오디오 파이프라인의 단계별 프로파일러가 들어왔습니다. ([#5])
- Include 카드에서 참조된 설정의 의존성을 스캔해 설정 디렉터리로 복사해 오는
  가져오기 흐름을 추가했습니다. ([#7])
- audiodg가 APO를 로드할 수 있도록 설치 디렉터리에 LOCAL SERVICE 권한을 주고,
  진단·복구 스크립트를 `tools/`에 두었습니다. ([#9], [#10])
- `setup-build.ps1`이 로컬 빌드 환경을 준비합니다. ([#8])

## v1.4.2 (2026-05-22)

TheFireKahuna 트리 위에서 포크를 시작한 버전입니다.

- **컨볼루션 꼬리 수정**: 프레임 크기가 바뀐 뒤 리버브 꼬리가 1000ms 부근에서
  끊기던 문제를 고치고, 새 하이브리드 컨볼루션 회귀 테스트로 보호했습니다.
  ([#2])
- 광범위한 현대화 리팩토링을 진행했습니다. 필터 설정 저장소와 COM 객체를
  RAII로 바꾸고, `nullptr`와 타입을 명시한 캐스트·버퍼 복사를 도입했으며, 큰
  구현 파일을 책임별로 나누고, 필터 factory 등록을 FilterEngine 밖으로
  옮겼으며, FilterEngine 동기화를 현대화했습니다. ([#1], [#2])
- 컨볼루션 파일 경로 처리를 다듬고 경로 파싱을 확장했습니다. ([#2])
- Velopack 릴리스 워크플로우와 업데이트 피드 연동이 들어왔습니다([#1], [#2]).
  GitHub Actions 빌드를 안정화했습니다. 의존성은 릴리스 자산에서 내려받고, Qt는
  CI에서 직접 설치하며, actions는 Node 24로 돌고, ARM64 빌드는 네이티브 MSVC
  환경을 씁니다.
- 카드 기반 모던 Editor UI의 첫 버전이 들어왔습니다.

[#1]: https://github.com/115dkk/EqualizerAPO-XT/pull/1
[#2]: https://github.com/115dkk/EqualizerAPO-XT/pull/2
[#3]: https://github.com/115dkk/EqualizerAPO-XT/pull/3
[#4]: https://github.com/115dkk/EqualizerAPO-XT/pull/4
[#5]: https://github.com/115dkk/EqualizerAPO-XT/pull/5
[#6]: https://github.com/115dkk/EqualizerAPO-XT/pull/6
[#7]: https://github.com/115dkk/EqualizerAPO-XT/pull/7
[#8]: https://github.com/115dkk/EqualizerAPO-XT/pull/8
[#9]: https://github.com/115dkk/EqualizerAPO-XT/pull/9
[#10]: https://github.com/115dkk/EqualizerAPO-XT/pull/10
[#11]: https://github.com/115dkk/EqualizerAPO-XT/pull/11
[#12]: https://github.com/115dkk/EqualizerAPO-XT/pull/12
[#13]: https://github.com/115dkk/EqualizerAPO-XT/pull/13
[#14]: https://github.com/115dkk/EqualizerAPO-XT/pull/14
[#15]: https://github.com/115dkk/EqualizerAPO-XT/pull/15
[#16]: https://github.com/115dkk/EqualizerAPO-XT/pull/16
[#17]: https://github.com/115dkk/EqualizerAPO-XT/pull/17
[#18]: https://github.com/115dkk/EqualizerAPO-XT/pull/18
[#19]: https://github.com/115dkk/EqualizerAPO-XT/pull/19
[#20]: https://github.com/115dkk/EqualizerAPO-XT/pull/20
[#21]: https://github.com/115dkk/EqualizerAPO-XT/pull/21
[#22]: https://github.com/115dkk/EqualizerAPO-XT/pull/22
[#23]: https://github.com/115dkk/EqualizerAPO-XT/pull/23
[#25]: https://github.com/115dkk/EqualizerAPO-XT/pull/25
[#26]: https://github.com/115dkk/EqualizerAPO-XT/pull/26
[#27]: https://github.com/115dkk/EqualizerAPO-XT/pull/27
[#28]: https://github.com/115dkk/EqualizerAPO-XT/pull/28
[#29]: https://github.com/115dkk/EqualizerAPO-XT/pull/29
[#30]: https://github.com/115dkk/EqualizerAPO-XT/pull/30
[#31]: https://github.com/115dkk/EqualizerAPO-XT/pull/31
[#32]: https://github.com/115dkk/EqualizerAPO-XT/pull/32
[#33]: https://github.com/115dkk/EqualizerAPO-XT/pull/33
[#34]: https://github.com/115dkk/EqualizerAPO-XT/pull/34
[#35]: https://github.com/115dkk/EqualizerAPO-XT/pull/35
[#36]: https://github.com/115dkk/EqualizerAPO-XT/pull/36
[#37]: https://github.com/115dkk/EqualizerAPO-XT/pull/37
[#38]: https://github.com/115dkk/EqualizerAPO-XT/pull/38
[#39]: https://github.com/115dkk/EqualizerAPO-XT/pull/39
[#40]: https://github.com/115dkk/EqualizerAPO-XT/pull/40
[#41]: https://github.com/115dkk/EqualizerAPO-XT/pull/41
[#42]: https://github.com/115dkk/EqualizerAPO-XT/pull/42
[#43]: https://github.com/115dkk/EqualizerAPO-XT/pull/43
[#44]: https://github.com/115dkk/EqualizerAPO-XT/pull/44
[#45]: https://github.com/115dkk/EqualizerAPO-XT/pull/45
[#46]: https://github.com/115dkk/EqualizerAPO-XT/pull/46
[#47]: https://github.com/115dkk/EqualizerAPO-XT/pull/47
[#49]: https://github.com/115dkk/EqualizerAPO-XT/pull/49
[#50]: https://github.com/115dkk/EqualizerAPO-XT/pull/50
[#51]: https://github.com/115dkk/EqualizerAPO-XT/pull/51
[#52]: https://github.com/115dkk/EqualizerAPO-XT/pull/52
[#54]: https://github.com/115dkk/EqualizerAPO-XT/pull/54
[#55]: https://github.com/115dkk/EqualizerAPO-XT/pull/55
[#56]: https://github.com/115dkk/EqualizerAPO-XT/pull/56
[#57]: https://github.com/115dkk/EqualizerAPO-XT/pull/57
[#58]: https://github.com/115dkk/EqualizerAPO-XT/pull/58
[#60]: https://github.com/115dkk/EqualizerAPO-XT/pull/60
[#61]: https://github.com/115dkk/EqualizerAPO-XT/pull/61
[#62]: https://github.com/115dkk/EqualizerAPO-XT/pull/62
[#63]: https://github.com/115dkk/EqualizerAPO-XT/pull/63
[#64]: https://github.com/115dkk/EqualizerAPO-XT/pull/64
[#70]: https://github.com/115dkk/EqualizerAPO-XT/pull/70
[#73]: https://github.com/115dkk/EqualizerAPO-XT/pull/73
[#75]: https://github.com/115dkk/EqualizerAPO-XT/issues/75
[#76]: https://github.com/115dkk/EqualizerAPO-XT/pull/76
[#78]: https://github.com/115dkk/EqualizerAPO-XT/pull/78
[#81]: https://github.com/115dkk/EqualizerAPO-XT/pull/81
[#85]: https://github.com/115dkk/EqualizerAPO-XT/pull/85
[#88]: https://github.com/115dkk/EqualizerAPO-XT/pull/88
[#94]: https://github.com/115dkk/EqualizerAPO-XT/pull/94
[#98]: https://github.com/115dkk/EqualizerAPO-XT/pull/98
[#105]: https://github.com/115dkk/EqualizerAPO-XT/pull/105
[#107]: https://github.com/115dkk/EqualizerAPO-XT/pull/107
[#108]: https://github.com/115dkk/EqualizerAPO-XT/pull/108
[#118]: https://github.com/115dkk/EqualizerAPO-XT/pull/118
[#124]: https://github.com/115dkk/EqualizerAPO-XT/pull/124
[#123]: https://github.com/115dkk/EqualizerAPO-XT/pull/123
[#120]: https://github.com/115dkk/EqualizerAPO-XT/pull/120
[#125]: https://github.com/115dkk/EqualizerAPO-XT/pull/125
[#126]: https://github.com/115dkk/EqualizerAPO-XT/pull/126
[#128]: https://github.com/115dkk/EqualizerAPO-XT/pull/128
[#129]: https://github.com/115dkk/EqualizerAPO-XT/pull/129
[#130]: https://github.com/115dkk/EqualizerAPO-XT/pull/130
[#132]: https://github.com/115dkk/EqualizerAPO-XT/pull/132
[#136]: https://github.com/115dkk/EqualizerAPO-XT/pull/136
[#137]: https://github.com/115dkk/EqualizerAPO-XT/pull/137
[#139]: https://github.com/115dkk/EqualizerAPO-XT/pull/139
