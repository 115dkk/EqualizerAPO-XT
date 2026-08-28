/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The one place a filter card can ask the analysis graph to show something.
*/

#pragma once

#include <QObject>

#include "Editor/analysis/AnalysisMetric.h"

// A filter card knows which reading makes its own filter legible - an all-pass
// is a flat line in magnitude and only says anything in phase or group delay -
// but a card sits several layers below the analysis dock and has no way to
// reach it. Walking up the parent chain to find the main window, or handing
// every card a pointer to a widget it does not otherwise use, would couple the
// card to the shell for one message.
//
// So the message travels on its own: a card emits a request, the shell decides
// whether to honour it, and neither knows about the other. The shell owns the
// decision, which matters because the request has to move the control-bar
// switch too, not just the graph - a graph showing phase while the switch reads
// "Mag" would be a lie about what the user is looking at.
class AnalysisViewController : public QObject
{
	Q_OBJECT

public:
	static AnalysisViewController* instance();

	// Ask the analysis graph to show this reading. Does nothing on its own; the
	// shell connects metricRequested when the analysis dock exists.
	void requestMetric(AnalysisMetric metric);

signals:
	void metricRequested(AnalysisMetric metric);

private:
	explicit AnalysisViewController(QObject* parent = nullptr);
};
