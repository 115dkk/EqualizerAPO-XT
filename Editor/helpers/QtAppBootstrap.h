/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <QLocale>
#include "services/registry/RegistryPaths.h"
#include <QString>

class QCoreApplication;
class QTranslator;

// Startup bootstrap shared by the two Qt apps (Editor, DeviceSelector), so
// the plugin-path anchoring and translator setup cannot drift apart between
// the two main()s.
namespace QtAppBootstrap
{
// Qt's plugins (platforms\qwindows.dll, imageformats, styles, tls) ship in a
// "qt" subfolder beside the executable. addLibraryPath() resolves a relative
// path against the current working directory, not the exe directory, so any
// launch whose working directory is not the install folder (a file-type
// association, a shortcut with a different "Start in", a debugger) leaves Qt
// unable to locate its platform plugin. For the elevated DeviceSelector it is
// also a security concern: a caller that controls the working directory
// could plant a Qt plugin DLL there. Anchor the plugin search to the
// executable's own directory instead. Call before constructing QApplication.
void addExecutableRelativePluginPath();

// Copies Qt's warnings, criticals and fatals into the product log (Editor.log,
// DeviceSelector.log) and then hands every message on to the handler that was
// installed before, so what reaches the console or the debugger is unchanged.
// Without it a qWarning from Qt or from our own code left no trace in the file
// a user sends (audit #348 TD-49). Debug and info messages are not copied.
// Call once, after the log destination is chosen.
void installMessageHandler();

// Applies the user's language choice as the default QLocale: the preference
// the Editor's language menu writes (EDITOR_REGPATH value "language") when
// set, the system locale otherwise. Shared so DeviceSelector follows the
// language chosen in the Editor.
void applyUserLocale();

// Installs the qtbase and <catalogName> catalogs from :/translations for the
// default locale. The translators are caller-owned so they outlive the
// QApplication (declare them in main() before calling exec()).
void installTranslators(QCoreApplication& app, const QString& catalogName, QTranslator& qtTranslator, QTranslator& appTranslator);
}
