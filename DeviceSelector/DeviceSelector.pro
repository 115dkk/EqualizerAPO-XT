#-------------------------------------------------
#
# DeviceSelector qmake project for CI builds
# This allows building DeviceSelector with qmake in CI
# while keeping the .vcxproj for local Visual Studio development
#
#-------------------------------------------------

QT += core gui widgets

TARGET = DeviceSelector
TEMPLATE = app

DEFINES += WIN32
DEFINES += _UNICODE
DEFINES += MUP_USE_WIDE_STRING
QMAKE_CXXFLAGS_RELEASE += /O2

PRECOMPILED_HEADER = stdafx.h

SOURCES += \
	main.cpp \
	DeviceSelector.cpp \
	DeviceTestDialog.cpp \
	DeviceTestThread.cpp \
	OpacityIconEngine.cpp \
	ReceiveThread.cpp \
	../helpers/ServiceHelper.cpp \
	stdafx.cpp

HEADERS += \
	DeviceSelector.h \
	DeviceTestDialog.h \
	DeviceTestThread.h \
	OpacityIconEngine.h \
	ReceiveThread.h \
	../helpers/ServiceHelper.h \
	resource.h \
	stdafx.h

FORMS += \
	DeviceSelector.ui \
	DeviceTestDialog.ui

RESOURCES += \
	DeviceSelector.qrc

TRANSLATIONS += \
	translations/DeviceSelector_de.ts \
	translations/DeviceSelector_en.ts \
	translations/DeviceSelector_fr.ts \
	translations/DeviceSelector_zh_CN.ts

# Include parent directory for shared headers
INCLUDEPATH += $$PWD/..

# Link against Common library and Windows libraries
LIBS += Kernel32.lib version.lib Shlwapi.lib authz.lib user32.lib advapi32.lib crypt32.lib

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

# UAC: Require Administrator
QMAKE_LFLAGS += /MANIFESTUAC:\"level=\'requireAdministrator\' uiAccess=\'false\'\"

RC_FILE = DeviceSelector.rc
