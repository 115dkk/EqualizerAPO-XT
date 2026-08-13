# EqualizerAPO-XT Skins — 사용 규약

이 프로젝트는 React 컴포넌트 번들이 아니다. Windows용 Qt Widgets 앱
(EqualizerAPO-XT Editor)의 5스킨 디자인 시스템에서 토큰·폰트·헌법 문서·실제
렌더 캡처만 동기화한 것이다. 화면은 일반 HTML/React 요소를 아래 토큰으로
직접 스타일링해 짓고, 목표는 이 앱의 스킨 문법에 맞는 목업이다.

## 셋업 (없으면 전부 무스타일)

모든 토큰은 스킨 선택자 아래에서만 정의된다. 루트 요소에 반드시 두 속성을
얹어라. 스킨은 `studio | minimal | soft | rack | matrix`, 모드는
`dark | light`다.

```html
<div data-skin="matrix" data-mode="dark"> …화면 전체… </div>
```

## 토큰 어휘 (이 이름 그대로, 새 색 발명 금지)

- 바탕: `--eapo-bg`(창 그라운드) `--eapo-surface` `--eapo-card`(카드 면)
  `--eapo-card-hover` `--eapo-card-selected` `--eapo-surface-raised`
  `--eapo-surface-sunken`(함몰 셀) `--eapo-graph`(그래프 그라운드)
- 잉크: `--eapo-text` `--eapo-muted` `--eapo-border`
  `--eapo-graph-grid-major` `--eapo-graph-grid-minor`
- 상태색: `--eapo-accent`(체결·선택·호버) `--eapo-accent2`(보조/컷 방향)
  `--eapo-success` `--eapo-warning` `--eapo-danger` `--eapo-focus-ring`
- 구조: `--eapo-radius` `--eapo-row-height` `--eapo-group-indent`
  `--eapo-card-rail-width`(matrix만 3px) `--eapo-font` `--eapo-mono`

## 다섯 스킨의 성격 (구속 규칙 요약)

- **studio**: 방송 콘솔. 라운드 8px, 블루/퍼플 듀얼 액센트.
- **minimal**: 잉크 단색주의. 라운드 0, 본문까지 DM Mono, 상태는 색보다
  형태(취소선·괄호·`!!`)로 말한다.
- **soft**: 파스텔. 라운드 14px, 행 44px로 가장 여유, success/warning/danger
  까지 파스텔 값이다.
- **rack**: 하드웨어 랙. 라운드 3px, 황동(`--eapo-accent`) 새김, 패널 물성.
- **matrix**: 라우팅 보드. **라운드 0·1px 룰만, 그림자/글로우 금지, AA 없는
  크리스프 선.** 신호등 색은 상태 전용(장식 금지), 시안 액센트는
  체결·선택·호버·신호 데이터 전용. 수치는 반드시 함몰 모노 박스 셀
  (`--eapo-surface-sunken` + 1px `--eapo-border` + `--eapo-mono`). 카드
  왼쪽에 3px 상태 레일(가동=`--eapo-success`, 바이패스=`--eapo-warning`),
  카드 본문 밴드는 `--eapo-bg`로 통째 채운 한 장의 보드 패널이다.

## 진실의 위치

스타일 값은 `tokens/<skin>.css`(Qt 소스 SkinThemeData.cpp에서 추출한 출하
값), 문법 규칙 전문은 `guidelines/<skin>.md`(한국어 헌법, 판례 포함), 실물
비주얼은 `guidelines/captures/<skin>/*.png`(실제 Qt 앱의 오프스크린 렌더)다.
스타일링 전에 대상 스킨의 헌법과 캡처를 먼저 읽어라.

## 관용 스니펫 (matrix 문법의 카드 한 장)

```html
<div data-skin="matrix" data-mode="dark" style="padding:16px">
  <div style="background:var(--eapo-card);border:1px solid var(--eapo-border);
              border-left:3px solid var(--eapo-success);border-radius:0">
    <div style="background:var(--eapo-card-hover);padding:6px 10px;
                display:flex;gap:8px;align-items:center">
      <span style="font-family:var(--eapo-mono);color:var(--eapo-muted);
                   border:1px solid var(--eapo-border);padding:2px 6px">B3</span>
      <b>Peaking</b>
    </div>
    <div style="background:var(--eapo-bg);border-top:1px solid var(--eapo-border);
                padding:10px">
      <span style="font-family:var(--eapo-mono);background:var(--eapo-surface-sunken);
                   border:1px solid var(--eapo-border);padding:4px 8px">1,000.00 Hz</span>
    </div>
  </div>
</div>
```
