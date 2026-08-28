/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AnalysisViewController.h"

AnalysisViewController::AnalysisViewController(QObject* parent)
	: QObject(parent)
{
}

AnalysisViewController* AnalysisViewController::instance()
{
	// Function-local static: constructed on first use, after QApplication
	// exists, and never before it.
	static AnalysisViewController controller;
	return &controller;
}

void AnalysisViewController::requestMetric(AnalysisMetric metric)
{
	emit metricRequested(metric);
}
