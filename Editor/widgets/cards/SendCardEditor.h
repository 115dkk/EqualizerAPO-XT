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
class RoutingSectionHead;
class RoutingView;

// Modern card body for a "Send:" line: which endpoint receives, how it
// treats what it receives, the latency and its compensation, and the
// channel routing in the active skin's routing view. The routing view's
// sources are this line's channels (device and virtual); its targets are
// the receiving endpoint's channels. The routing is a section that folds
// down to one line (RoutingSectionHead).
class SendCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	SendCardEditor(FilterTable* filterTable, const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void setChannelFlow(const ChannelFlowAtLine& flow) override;
	// A routing section the user folded stays folded when the row is rebuilt
	// and when the file is opened again. Nothing is stored for a section the
	// user never touched; it is open.
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

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
	void updateRoutingSummary();
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
	// The routing section's fold toggle and folded summary. Hidden while the
	// line has no endpoint, because then there is no routing view to fold.
	RoutingSectionHead* routingHead = nullptr;
	QVBoxLayout* routingLayout = nullptr;
	RoutingView* routingView = nullptr;
	// True once the user folded or opened the section, or a stored choice
	// was loaded.
	bool routingFoldChosen = false;
	bool updating = false;
};
