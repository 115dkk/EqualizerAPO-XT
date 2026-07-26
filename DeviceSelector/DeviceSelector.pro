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
DEFINES += NOMINMAX
QMAKE_CXXFLAGS_RELEASE += /O2

PRECOMPILED_HEADER = stdafx.h

SOURCES += \
	../Editor/helpers/QtAppBootstrap.cpp \
	../Editor/skins/SkinThemeData.cpp \
	main.cpp \
	DeviceListDelegate.cpp \
	DeviceSelector.cpp \
	DeviceTestDialog.cpp \
	DeviceTestThread.cpp \
	DisclosureHeader.cpp \
	OpacityIconEngine.cpp \
	PreviewDevices.cpp \
	ReceiveThread.cpp \
	SkinButton.cpp \
	skins/DeviceSkinPainter.cpp \
	skins/StudioDeviceSkin.cpp \
	skins/MinimalDeviceSkin.cpp \
	skins/SoftDeviceSkin.cpp \
	skins/RackDeviceSkin.cpp \
	skins/MatrixDeviceSkin.cpp \
	../helpers/ServiceHelper.cpp \
	../helpers/ApoRegistration.cpp \
	../helpers/AudioEngineAccess.cpp \
	stdafx.cpp

HEADERS += \
	../Editor/helpers/QtAppBootstrap.h \
	../Editor/skins/SkinThemeData.h \
	DeviceListDelegate.h \
	DeviceSelector.h \
	DeviceTestDialog.h \
	DeviceTestThread.h \
	DisclosureHeader.h \
	OpacityIconEngine.h \
	PreviewDevices.h \
	ReceiveThread.h \
	SkinButton.h \
	skins/DeviceSkinPainter.h \
	../helpers/ServiceHelper.h \
	../helpers/ApoRegistration.h \
	../helpers/AudioEngineAccess.h \
	resource.h \
	stdafx.h

FORMS += \
	DeviceSelector.ui \
	DeviceTestDialog.ui

RESOURCES += \
	DeviceSelector.qrc \
	DeviceSelectorSkins.qrc

TRANSLATIONS += \
	translations/DeviceSelector_de.ts \
	translations/DeviceSelector_en.ts \
	translations/DeviceSelector_fr.ts \
	translations/DeviceSelector_ko.ts \
	translations/DeviceSelector_zh_CN.ts

# Include parent directory for shared headers
INCLUDEPATH += $$PWD/..

# Link against Common library and Windows libraries
LIBS += Kernel32.lib version.lib Shlwapi.lib authz.lib user32.lib advapi32.lib crypt32.lib

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

# UAC: Require Administrator
QMAKE_LFLAGS += /MANIFESTUAC:\"level=\'requireAdministrator\' uiAccess=\'false\'\"

RC_FILE = DeviceSelector.rc
