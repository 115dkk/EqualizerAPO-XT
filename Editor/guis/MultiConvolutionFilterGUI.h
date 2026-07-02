/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "Editor/IFilterGUI.h"

class QLineEdit;

// Legacy (frozen-fallback) row GUI for a "MultiConvolution:" line. Deliberately
// minimal, built in code rather than from a .ui: the mapping prefix as plain
// text ("L=0+1 R=2+3", or just "L" for the simple form) and an impulse-response
// path field with a file picker. The modern card editor
// (widgets/cards/MultiConvolutionCardEditor) is the canonical UI with the
// per-skin routing view; this exists so the LegacyRows render mode still edits
// the line without destroying it.
class MultiConvolutionFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	MultiConvolutionFilterGUI(const QString& configPath, const QString& mappingsText, const QString& path);

	void store(QString& command, QString& parameters) override;

private slots:
	void selectFile();

private:
	QString configPath;
	QLineEdit* mappingsEdit;
	QLineEdit* pathEdit;
};
