/*
	This file is part of EqualizerAPO-XT.
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
