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
DEFINES += NOMINMAX
# Release channel baked in at build time (mirrors Editor.pro). When unset, a
# local build falls back to VelopackUpdateInfo::defaultChannel()'s per-arch
# guess for feed lookups; CI always passes the channel explicitly.
!isEmpty(EAPO_UPDATE_CHANNEL) {
	DEFINES += EAPO_UPDATE_CHANNEL=\\\"$$EAPO_UPDATE_CHANNEL\\\"
}
QMAKE_CXXFLAGS_RELEASE += /O2

PRECOMPILED_HEADER = stdafx.h

SOURCES += \
	../Editor/helpers/QtAppBootstrap.cpp \
	main.cpp \
	UpdateChecker.cpp \
	UpdateInfoFormatter.cpp \
	VelopackUpdateInfo.cpp \
	AutoSizeTextEdit.cpp \
	../services/logging/LogHelper.cpp \
	../services/registry/RegistryHelper.cpp \
	../text/StringHelper.cpp \
	stdafx.cpp

HEADERS += \
	../Editor/helpers/QtAppBootstrap.h \
	UpdateChecker.h \
	UpdateInfoFormatter.h \
	VelopackUpdateInfo.h \
	AutoSizeTextEdit.h \
	../services/logging/LogHelper.h \
	../text/StringHelper.h \
	../services/registry/RegistryHelper.h \
	resource.h \
	stdafx.h

FORMS += \
	UpdateChecker.ui

RESOURCES += \
	UpdateChecker.qrc

TRANSLATIONS += \
	translations/UpdateChecker_de.ts \
	translations/UpdateChecker_fr.ts \
	translations/UpdateChecker_ko.ts \
	translations/UpdateChecker_zh_CN.ts

# Include parent directory for shared headers
INCLUDEPATH += $$PWD/..

# Link against Common library and Windows libraries
LIBS += -lSecur32 -ltaskschd Kernel32.lib version.lib ole32.lib oleaut32.lib Shlwapi.lib user32.lib advapi32.lib crypt32.lib uuid.lib authz.lib

# Include Common.lib
LIBS += Common.lib
include($$PWD/../common.pri)
contains(QT_ARCH, arm64) {
	EAPO_COMMON_ARCH = ARM64
} else {
	EAPO_COMMON_ARCH = x64
}
build_pass:CONFIG(debug, debug|release) {
	QMAKE_LIBDIR += "../$$EAPO_COMMON_ARCH/Debug"
} else {
	QMAKE_LIBDIR += "../$$EAPO_COMMON_ARCH/Release"
}

RC_FILE = UpdateChecker.rc
