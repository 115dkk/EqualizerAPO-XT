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

#include <vector>

#include "Editor/IFilterGUI.h"
#include "StageSelectionModel.h"

class QToolButton;

// Modern card body for a "Stage:" line (Phase 1, the neutral base shared by
// every skin): checkable chips in the same grammar as the Device and Channel
// cards, arranged in two captioned lanes that mirror the engine's pipelines —
// Playback holds pre-mix and post-mix in signal order behind a chain arrow,
// Recording holds capture. The written line matches what the legacy checkbox
// GUI produced for the same selection, except that tokens outside the
// vocabulary survive as a muted inert chip instead of being dropped.
class StageCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit StageCardEditor(const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chipToggled();

private:
	StageSelectionModel model;

	struct StageChip
	{
		QToolButton* button = nullptr;
		QString token;
	};
	std::vector<StageChip> chips;
};
