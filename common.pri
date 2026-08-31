# Shared qmake fragment for the three Qt apps (Editor, DeviceSelector,
# UpdateChecker). The SIMD flag selection and its misconfiguration gate used
# to be pasted into each .pro, so every change had to be made three times.
# QMAKE_LIBDIR stays in each .pro: the Editor links the dependency lib
# directories directly while the satellite apps link the MSBuild output tree.
# (audit #146 TD012)
# The C++ standard mode is set once here for all three Qt applications.
CONFIG += c++20

contains(QT_ARCH, arm64) {
	# ARM64 builds take NEON without an /arch switch.
} else:!isEmpty(EAPO_SIMD_FLAGS) {
	QMAKE_CXXFLAGS += $$EAPO_SIMD_FLAGS
} else:equals(EAPO_SIMD_BASELINE, 1) {
	# SSE2 baseline: the MSVC x64 default, no /arch switch.
} else {
	# A non-ARM64 build that passes no SIMD selection used to fall back to /arch:AVX2
	# while still labelling the binary with whatever EAPO_UPDATE_CHANNEL it was given.
	# That silently mislabels a misconfigured local build as AVX2. Fail loudly instead;
	# the documented local + CI command passes EAPO_SIMD_FLAGS and EAPO_UPDATE_CHANNEL
	# (e.g. EAPO_SIMD_FLAGS=/arch:AVX2 EAPO_UPDATE_CHANNEL=x64-avx2).
	error("EAPO_SIMD_FLAGS must be set for x64 builds (e.g. EAPO_SIMD_FLAGS=/arch:AVX2), or pass EAPO_SIMD_BASELINE=1 for the SSE2 baseline. Also set EAPO_UPDATE_CHANNEL to the matching channel (e.g. x64-avx2). See .github/simd-variants.psd1 for the variant/channel map.")
}
