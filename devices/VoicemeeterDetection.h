#pragma once

// Audit #250 F021: the Voicemeeter install-detection vocabulary used to be
// copied verbatim into VoicemeeterAPOInfo.cpp and VoicemeeterClient.cpp -
// only one of which runs under the registry port's tests. The literals live
// here once; each consumer keeps its own lookup code (they read through
// different registry facades on purpose).
#define voicemeeterKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}"
#define voicemeeterWowKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:Voicemeeter {17359A74-1236-5467}"
#define uninstallStringValueName L"UninstallString"
