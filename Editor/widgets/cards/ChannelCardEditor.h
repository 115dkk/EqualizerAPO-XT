/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	In-place channel multi-select for "Channel:" card rows. Replaces the
	change-button-plus-dialog flow with checkable chips directly in the card
	body: one chip per device channel, an ALL chip, chips for custom/virtual
	channels named in the line, and a small field to add new custom names.
	Selection state and serialization live in ChannelSelectionModel; the
	written line goes through ChannelCommand and stays byte-identical with
	what the legacy dialog produced for equivalent selections.
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "ChannelSelectionModel.h"

class QHBoxLayout;
class QLineEdit;
class QToolButton;

class ChannelCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit ChannelCardEditor(const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void configureChannels(std::vector<std::wstring>& channelNames) override;
	void configureSelectedChannels(std::vector<std::wstring>& selectedChannels) override;

private slots:
	void allToggled(bool checked);
	void customEntered();

private:
	void reloadChips();
	// Re-apply the model's states to the existing chips (no rebuild, so it
	// is safe inside a chip's own toggled signal).
	void syncChipStates();
	void commitSelection();

	ChannelSelectionModel model;
	// Current parameter text: the line's original parameters until the first
	// edit, the model's canonical serialization afterwards.
	QString parameters;
	std::vector<std::wstring> deviceChannels;
	QToolButton* allChip = nullptr;
	QHBoxLayout* chipLayout = nullptr;
	QLineEdit* customEdit = nullptr;
	bool updating = false;
};
