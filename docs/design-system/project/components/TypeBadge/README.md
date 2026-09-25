# TypeBadge

카드 헤더의 종류 배지와 채널 배지다. 종류 배지의 내용물은 카탈로그 픽토그램(`Icons` 묶음의 `eq-*`, `file-include`, `plugin`, `route-channels`, `comment-bubble`, `logic-if` 등)이고 영어 모노그램은 미지 명령의 폴백으로만 남는다. 배지 스타일은 `SkinTokens.badgeStyle`(ColorPill, OutlineOnly, SoftPill, WireframeBorder)이 고르고 잉크는 `ISkin::typeBadgeInk`가 답한다.

## 공유 계약

배지는 실루엣을 바꾸지 않는다. 종류의 뜻은 명령 타입 색 `type-*`에서 오되 그 색을 어떻게 쓰는지는 스킨의 몫이고, 채널 배지는 데이터라 어느 스킨에서든 채널 식별 색 `channel-*`를 그대로 쓴다(`ChannelIdentity.h`, Copy 라우팅과 같은 표). 표 밖의 채널(ALL, 번호 채널)은 `channel-neutral`이다. 장치에 없는 채널(가상 채널)의 배지는 선을 파선으로 긋는다. 판정은 `ChannelIdentity::isVirtual` 하나가 하고, 장치를 모르면 7.1 배치를 기준으로 삼는다. 미리보기도 7.1 장치를 가정하므로 VC는 가상이고 L은 아니다. 비활성 배지는 어느 스킨에서도 경고색을 입지 않는다.

## 스킨별 재질

- **studio:** 점등된 유리 칩이다. 행의 빛 색 잉크(BiQuad는 `studio-band-*`, 그 외는 `type-*`)에 같은 색의 반투명 채움(다크 α 0.15/보더 0.42, 라이트 0.10/0.45), 라운드 `studio-radius`. 비활성이면 칩이 꺼진다(`studio-muted` 잉크, 투명 채움). 채널 배지는 알약(ColorPill)이다. 채널 색의 1px 선과 글자에, 같은 색을 다크 α 70/255(라이트 48/255)로 옅게 채운다.
- **minimal:** 외곽선만(OutlineOnly). 1px 타입 색 보더에 타입 색 잉크, 채움 없음, 라운드 0. 채널은 배지가 아니라 `minimal-ch-*` 맨 잉크다. 콘솔 그라운드에 채워진 칩은 GUI 어휘이기 때문이다.
- **soft:** 파스텔 스타디움 칩(SoftPill). 타입 색의 색조를 유지한 채 softPastelize(채도 상한 0.50/0.55, 명도 0.62/0.60)로 선반에 올린 불투명 채움 위에 `soft-on-ink`의 픽토그램. 채널 배지는 파스텔로 바꾸지 않고 studio와 같은 알약에 채널 색을 그대로 쓴다. 비활성은 잠듦(점선 윤곽 + `soft-muted`).
- **rack:** 와이어프레임(WireframeBorder). 채움 없는 1px 타입 색 와이어 안에 픽토그램을 타입 색 잉크로 인쇄한다(패널 실크스크린의 기호 인쇄), 라운드 `rack-radius`보다 작은 2px. 비활성은 전원 내린 필름이다. 채널 배지는 같은 와이어(1.2px, 라운드 `rack-radius`)에 채널 색을 다크 α 35/255(라이트 22/255)로 옅게 채운다.
- **matrix:** 단색 코드 셀. `typeBadgeStyle`이 타입 색 인자를 의도적으로 무시하고 `matrix-text` 잉크 + 1px 보더만 남긴다(색 배급제). 채널 배지는 채널 색 외곽선의 직각 셀이고, rack과 같은 비율로 옅게 채운다. 비활성은 점선 룰.

## 상태

배지 자체의 상태는 켜짐과 꺼짐뿐이고 호버·선택은 행이 답한다. 꺼짐은 studio 소등, minimal·matrix 보조 잉크, soft 잠듦, rack 전원 내림이다.

## 소비자가 주는 것

명령 타입(카탈로그 id와 `type-*` 색), BiQuad면 필터 가족, 채널 이름 목록, enabled 플래그. hand-written from `Editor/widgets/FilterCardRow.cpp`, `Editor/widgets/ChBadge.cpp`(채널 색은 `Editor/widgets/routing/ChannelIdentity.h`), `Editor/widgets/FilterCommandCatalog.cpp`.
