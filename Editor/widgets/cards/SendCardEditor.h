/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "SendCardModel.h"

class FilterTable;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class RoutingView;

// Modern card body for a "Send:" line: which endpoint receives, how it
// treats what it receives, the latency and its compensation, and the
// channel routing in the active skin's routing view. The routing view's
// sources are this line's channels (device and virtual); its targets are
// the receiving endpoint's channels.
class SendCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	SendCardEditor(FilterTable* filterTable, const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void setChannelFlow(const ChannelFlowAtLine& flow) override;

private slots:
	void targetChosen(int index);
	void modeChosen(int index);
	void latencyEdited();
	void latencyTyped();
	void compensateToggled(bool on);
	void routingEdited();

private:
	void loadEndpoints();
	void rebuildRoutingView();
	void updateWarning();

	FilterTable* filterTable = nullptr;
	SendCardModel model;
	std::vector<std::wstring> rowSources;
	std::vector<std::wstring> ownChannels;

	QComboBox* targetCombo = nullptr;
	QComboBox* modeCombo = nullptr;
	QLineEdit* latencyEdit = nullptr;
	QCheckBox* compensateCheck = nullptr;
	QLabel* warning = nullptr;
	QVBoxLayout* routingLayout = nullptr;
	RoutingView* routingView = nullptr;
	bool updating = false;
};
