#-------------------------------------------------
#
# UpdateChecker qmake project for CI builds
# This allows building UpdateChecker with qmake in CI
# while keeping the .vcxproj for local Visual Studio development
#
#-------------------------------------------------

QT += core gui widgets network

TARGET = UpdateChecker
TEMPLATE = app

DEFINES += WIN32
DEFINES += _UNICODE
DEFINES += MUP_USE_WIDE_STRING
!isEmpty(EAPO_UPDATE_CHANNEL) {
	DEFINES += EAPO_UPDATE_CHANNEL=\\\"$$EAPO_UPDATE_CHANNEL\\\"
}
QMAKE_CXXFLAGS_RELEASE += /O2

PRECOMPILED_HEADER = stdafx.h

SOURCES += \
	main.cpp \
	UpdateChecker.cpp \
	UpdateInfoFormatter.cpp \
	VelopackUpdateInfo.cpp \
	AutoSizeTextEdit.cpp \
	../helpers/StringHelper.cpp \
	stdafx.cpp

HEADERS += \
	UpdateChecker.h \
	UpdateInfoFormatter.h \
	VelopackUpdateInfo.h \
	AutoSizeTextEdit.h \
	../helpers/StringHelper.h \
	../helpers/RegistryHelper.h \
	resource.h \
	stdafx.h

FORMS += \
	UpdateChecker.ui

RESOURCES += \
	UpdateChecker.qrc

TRANSLATIONS += \
	translations/UpdateChecker_de.ts \
	translations/UpdateChecker_fr.ts \
	translations/UpdateChecker_zh_CN.ts

# Include parent directory for shared headers
INCLUDEPATH += $$PWD/..

# Link against Common library and Windows libraries
LIBS += -lSecur32 -ltaskschd Kernel32.lib version.lib oleaut32.lib Shlwapi.lib user32.lib advapi32.lib crypt32.lib uuid.lib authz.lib

# Include Common.lib
LIBS += Common.lib
contains(QT_ARCH, arm64) {
	build_pass:CONFIG(debug, debug|release) {
		QMAKE_LIBDIR += "../ARM64/Debug"

	} else {
		QMAKE_LIBDIR += "../ARM64/Release"
	}
} else:!isEmpty(EAPO_SIMD_FLAGS) {
	QMAKE_CXXFLAGS += $$EAPO_SIMD_FLAGS
	build_pass:CONFIG(debug, debug|release) {
		QMAKE_LIBDIR += "../x64/Debug"

	} else {
		QMAKE_LIBDIR += "../x64/Release"
	}
} else:equals(EAPO_SIMD_BASELINE, 1) {
	build_pass:CONFIG(debug, debug|release) {
		QMAKE_LIBDIR += "../x64/Debug"

	} else {
		QMAKE_LIBDIR += "../x64/Release"
	}
} else {
	# A non-ARM64 build that passes no SIMD selection used to fall back to /arch:AVX2
	# while still labelling the binary with whatever EAPO_UPDATE_CHANNEL it was given.
	# That silently mislabels a misconfigured local build as AVX2. Fail loudly instead;
	# the documented local + CI command passes EAPO_SIMD_FLAGS and EAPO_UPDATE_CHANNEL
	# (e.g. EAPO_SIMD_FLAGS=/arch:AVX2 EAPO_UPDATE_CHANNEL=x64-avx2).
	error("EAPO_SIMD_FLAGS must be set for x64 builds (e.g. EAPO_SIMD_FLAGS=/arch:AVX2), or pass EAPO_SIMD_BASELINE=1 for the SSE2 baseline. Also set EAPO_UPDATE_CHANNEL to the matching channel (e.g. x64-avx2). See .github/simd-variants.psd1 for the variant/channel map.")
}

RC_FILE = UpdateChecker.rc
