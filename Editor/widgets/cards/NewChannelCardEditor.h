/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "Editor/IFilterGUI.h"
#include "NewChannelListModel.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;

// Modern card body for a "NewChannel:" line (the neutral base every skin
// dresses through QSS): one chip per declared name, a field that adds more,
// and a line of text when the engine would skip the line. A chip removes
// its name when clicked. There is no routing view: the line only names
// channels, it moves no audio.
class NewChannelCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit NewChannelCardEditor(const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void setChannelFlow(const ChannelFlowAtLine& flow) override;

private slots:
	void addEntered();
	void addEdited();

private:
	void reloadChips();
	void updateWarning();
	void commit();

	NewChannelListModel model;
	QHBoxLayout* chipLayout = nullptr;
	QLineEdit* addEdit = nullptr;
	QLabel* warning = nullptr;
};
