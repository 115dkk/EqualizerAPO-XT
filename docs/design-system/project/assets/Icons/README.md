# Icons

Editor의 modern 아이콘 세트(`Editor/icons/modern/`) 60종을 그대로 옮겼다. 24px 뷰박스에 1.8px 라운드 스트로크의 단일 잉크 선화이고, 파일 안의 잉크는 검정(`#000000`)으로 굳어 있다(`check-light`와 `dash-light`만 흰색, `check-dark`와 `dash-dark`는 검정). Editor는 런타임에 스킨 잉크로 다시 칠하므로 `<img>`로 쓰면 검정 그대로 보인다. 잉크를 바꾸려면 경로를 인라인해 `stroke="currentColor"`를 준다.

명령 픽토그램은 `eq-peaking`, `eq-lowpass`, `eq-highpass`, `eq-bandpass`, `eq-lowshelf`, `eq-highshelf`, `eq-notch`, `eq-allpass`(BiQuad 8형), `graphic-eq`, `preamp-gain`, `delay-clock`, `route-channels`(Copy), `channel-select`, `file-include`, `plugin`(VST), `device-speaker`, `stage-chain`, `loudness`, `subwoofer-routing`, `multi-convolution`, `waveform`(Convolution·Velvet), `comment-bubble`, `logic-if`(If 가족의 판정 다이아몬드), `logic-eval`(fx)이다. 배지와 픽커 타일의 내용물이고 잉크는 그 스킨의 타입 잉크다.

캐럿과 체크는 `chevron-down`, `chevron-up`, `chevron-right`, `check-light`, `check-dark`, `dash-light`, `dash-dark`다. 콤보와 스핀박스의 화살은 전 스킨이 `chevron-*`를 12px로 쓴다(`SkinThemeData::comboArrowOverride`).

툴바와 편집 동작은 `file-new`, `folder-open`, `save`, `save-as`, `import`, `undo`, `redo`, `cut`, `copy`, `paste`, `trash`, `select-all`, `code-line`(원본 명령 편집 `</>`), `pencil`(Include의 파일 내용 편집), `alert-triangle`이다. 파일 대화상자는 `nav-back`, `nav-forward`, `folder-up`, `folder-new`, `view-list`, `view-detail`을 쓴다. 창 캡션은 `window-min`, `window-max`, `window-restore`, `window-close`다.

`matrix-grid-dark`와 `matrix-grid-light`는 matrix 메뉴바의 24px 모눈 타일, `soft-toggle-knob-on`과 `soft-toggle-knob-off`는 soft 토글의 손잡이 이미지로, 픽토그램이 아니라 QSS 배경 이미지다. rack은 아이콘을 포함한 어떤 비트맵 에셋도 들이지 않고 전부 페인트로 그린다.
