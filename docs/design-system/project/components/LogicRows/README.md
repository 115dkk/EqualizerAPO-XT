# LogicRows

If/ElseIf/Else/EndIf와 Eval 행, 그리고 If 블록의 스코프를 거터에 그리는 각 스킨의 문법이다. 스코프 표시는 `ISkin::paintScopeGutter`가 맡고, 분기 판정(`branchState`)·스킵 여부(`lineSkipped`)·계산값(`evalText`)은 마지막 분석 로드의 참고용 사실이라 페인트 시점에만 읽는다. 미리보기는 갤러리 `logic_normal`의 앞부분(Eval, 참인 If, 거짓인 중첩 If, Else if)을 옮겼다.

## 공유 계약

minimal을 제외한 네 스킨은 `logicSiblingsIndentAsMembers = true`로 ElseIf/Else/EndIf를 멤버 깊이에 앉혀 거터 표시가 그 얼굴들을 통과하게 한다. minimal만 소스 코드처럼 자기 If와 같은 칸에 정렬한다. 분기 진위에 어느 스킨도 경고색을 붙이지 않고, 거짓 분기가 삼킨 구간은 소등·침강·이완이지 경보가 아니다. 취소선은 바이패스의 문법이라 스킵 행에 걸지 않는다. Eval의 값은 램프가 아니라 데이터다.

## 스킨별 재질

- **studio:** 거터를 흘러내리는 게이트 빔이다. 노브 호의 스트로크 사다리를 세로로 세운 것이고 유리 표면과 잉크는 빔 색을 입지 않는다. 살아 있는 구간은 점등, 거짓 분기가 삼킨 줄은 자기 구간만 감광된 잔광이다. If/ElseIf/Else 스테이션은 앵커 도트로 답하고 택한 분기만 점등한다. 빔은 헤드 행 아래 여백에서 태어나 EndIf에서 페이드아웃한다. Eval은 헤더 오른쪽에 `= 값`을 흐린 모노 판독으로 얹는다. If/Eval 행 자체는 시그널 램프 없는 불 꺼진 유리다.
- **minimal:** 코드 에디터의 들여쓰기 가이드다. 스코프 레벨마다 `minimal-border`의 크리스프 1px 헤어라인 하나뿐이고 거짓 구간을 지나는 가이드는 점선이 된다. 스킵 줄은 배경이 비활성과 같은 한 스텝 아래로 가라앉고 라벨 잉크가 보조로 물러난다. 헤더 오른쪽의 워치 레지스터 칼럼이 If의 판정(`TRUE` 본문 잉크, `FALSE` 보조 잉크, 미분석·단락은 한 단 더 가라앉은 em 대시, `ERR`는 굵은 본문 잉크)과 Eval의 `= 값`을 찍는다.
- **soft:** 단순한 If/Eval 줄은 설정 앱의 문장으로 되읽는다(`If outputChannelCount is at least 6`, `Otherwise`, `End of the rule`, `Set x to 5`). If 블록은 파스텔 팔이 안는다. 스코프 레벨마다 거터의 둥근 바(값 호 파스텔, 4px, 스타디움 캡)가 If 문장 아래에서 태어나 EndIf 중심선에서 캡으로 닫힌다. 거짓 구간은 노브 트랙의 상시 파스텔로 이완될 뿐이고 램프도 적색도 글리프도 없다.
- **rack:** 거터의 릴레이 전원 버스다. 랙 개구부의 어두운 솔기(`rack-seam`) 케이싱에 앰버(`rack-accent`) 코어를 물린 급전선이 If 헤드 아래 급전점에서 태어나 EndIf 접점 블록에서 끝난다. 분기 판정은 paintLed 문법의 주얼 램프(택함 녹, 평가 오류만 적, 거짓·단락·미분석은 같은 꺼진 돔). 거짓 구간은 코어가 카드 색으로 물러난 앰버 딤이다. Eval 본문은 AUX 프로그래밍 LCD를 탄다.
- **matrix:** 인쇄된 브래킷이다. 열린 스코프마다 들여쓰기 밴드 중심의 크리스프 1px 뮤트 룰이 헤드 행 아래에서 열려 EndIf 행에서 반 피치의 수평 틱(L-코너)으로 닫힌다. 분기 판정은 거터의 5px 램프(택함 실선 녹, 거짓 속 빈 녹, 오류 실선 적, 미도달 속 빈 뮤트). 스킵 줄은 결항 게시(점선 외곽 룰 + `matrix-border` 레일 + 저알파 헤더)다. If 가족은 버스 문자 `F`, Eval은 `E`를 받고 계산값은 헤더 우측의 박스 함몰 모노 셀에 `= 값`으로 게시한다.

## 상태

행의 hover·selected·disabled는 카드 행의 문법을 따르고, 분기 상태는 위의 스테이션·워치 칼럼·램프·브래킷이 말한다. 편집과 다음 분석 사이에는 마지막 로드의 사실이 낡아 있을 수 있다.

## 소비자가 주는 것

각 행의 종류(If/ElseIf/Else/EndIf/Eval)와 깊이, 조건 원문, 마지막 분석의 분기 판정과 스킵 여부, Eval 계산값. hand-written from `Editor/skins/<id>/<Skin>.CommandRows.cpp` (paintScopeGutter) and the constitutions' 동적 명령어 sections, against the gallery shot `logic_normal`.
