# EqualizerAPO-XT 위키
EqualizerAPO-XT는 Jonas Thedering의 [Equalizer APO](https://sourceforge.net/projects/equalizerapo/)를 바탕으로 한 Windows 시스템 전체 이퀄라이저입니다. XT 포크는 기존 설정 방식을 그대로 유지하면서 64비트(double) 내부 처리, 길이 제한이 없는 컨볼버 임펄스 응답, BRIR/크로스피드용 MultiConvolution, Hilbert·Velvet 위상/비상관화 도구, 명시적 VST3 주 버스 레이아웃, 독립 VST3 플러그인과 코어를 공유하는 한 줄 서브우퍼 라우팅, 재생 장치와 같은 자리에 선 녹음 장치, 그리고 ASIO를 더했습니다. 같은 `config.txt`가 `<드라이버> (EQ APO XT)` 항목을 통해 ASIO 스트림에 적용되고, 어떤 Windows 엔드포인트든 WASAPI 독점 모드로 ASIO 응용 프로그램에 제공되어 독점 모드로 듣는 사람도 EQ를 잃지 않습니다. 이식 가능한 SIMD 변형은 AVX10.1까지의 x64와 ARM64/NEON을 지원하며, 자동 감지 Velopack 설치 파일이 맞는 빌드를 고르고 자동 업데이트가 최신 상태를 유지합니다.

이 위키는 원본 Equalizer APO 문서의 구성을 따르되, 설치와 패키징 부분을 XT 빌드에 맞게 고쳐 썼습니다.

## 문서
* **[사용자 문서](Korean-Documentation)** — 설치, 첫 설정, 문제 해결을 다룹니다. 여기서 시작하세요.
* **[설정 레퍼런스](Korean-Configuration-reference)** — 설정 파일 형식과 지원하는 모든 명령입니다. 고급 사용자용입니다.
* **[개발자 문서](Korean-Developer-documentation)** — 소스 빌드, 프로젝트 구조, Windows에 APO를 등록하는 방법입니다.

## 다른 언어
* [English (영어)](Home)

## 라이선스와 출처
EqualizerAPO-XT는 원본 Equalizer APO와 같은 [GNU General Public License 버전 2 이상](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)으로 배포됩니다. ASIO 래퍼(`EqualizerAPOAsio.dll`)는 Steinberg ASIO SDK 2.3.4를 SDK의 GPL 버전 3 선택지로 써서 빌드하므로, 래퍼와 그것을 담은 설치 파일은 GPL 버전 3으로 배포되며 두 전문이 `License.txt`와 `License-gpl-3.0.txt`로 설치됩니다. ASIO는 Steinberg Media Technologies GmbH의 상표이자 소프트웨어입니다. VST3 호스팅에는 MIT 라이선스인 Steinberg VST3 *pluginterfaces*를 쓰고, VST2 인터페이스 헤더는 독립적으로 다시 작성한 BSD-2 클린룸 구현본이며, `SubwooferRoutingCore` DSP 라이브러리와 `EAPO XT Subwoofer Routing` VST3 플러그인은 MIT 라이선스입니다. Mephistos(디시인사이드)님이 죽어 있던 VST3 패널 미터를 진단하고, 패널 피드의 바탕이 된 WASAPI 루프백 패치를 기여했으며, 검증에 쓴 플러그인을 제공해 주셨습니다. 자세한 내용은 [저장소](https://github.com/115dkk/EqualizerAPO-XT)를 보세요.
