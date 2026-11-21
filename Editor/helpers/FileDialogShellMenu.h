/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

#include <functional>

class QFileDialog;

// Explorer's context menu inside the skinned (widget-based) QFileDialog.
// Qt's own right-click menu there offers Rename, Delete, hidden files and
// New Folder and nothing from the shell, so search tools, archivers and
// Properties were out of reach (issue #508). attach() replaces it: a
// right-click on files or folders opens their shell menu, a right-click on
// empty space opens the folder background's.
namespace FileDialogShellMenu
{
// What one right-click asks for. items holds the absolute paths (native
// separators) the menu is for and is empty for the folder background;
// folder is the directory the dialog shows; globalPos is where the click
// landed, in Qt's device-independent screen coordinates.
struct Request
{
	QStringList items;
	QString folder;
	QPoint globalPos;
};

using Presenter = std::function<void (QFileDialog&, const Request&)>;

void attach(QFileDialog& dialog);

// Test seam: the self-test records requests instead of opening a menu on
// the desktop. An empty presenter restores the shell one.
void setPresenterForTest(Presenter presenter);
}
