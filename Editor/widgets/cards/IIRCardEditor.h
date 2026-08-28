/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <vector>

#include <QVector>

#include "Editor/IFilterGUI.h"

class FlowLayout;
class ValueScrubBox;
class ValueScrubIntBox;

// The custom-coefficient IIR card body ("Filter: ON IIR Order N Coefficients
// b0..bN a0..aN"): the order as a value-scrub field plus the two coefficient
// vectors as labeled rows of scrub fields. Changing the order re-fits the
// rows in place; every edit serializes through the shared IIRCommand codec so the
// engine parser and the card agree on one format. BiQuad "Filter" lines never
// reach this editor - the registry lambda rejects them and they keep their
// legacy knob GUI through the fallback chain.
class IIRCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit IIRCardEditor(unsigned order, const std::vector<double>& coefficients, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void orderChanged(int value);
	void coefficientChanged();

private:
	void rebuildRows();
	void rebuildRow(FlowLayout* flow, QVector<ValueScrubBox*>& boxes, const std::vector<double>& values, QChar prefix);

	ValueScrubIntBox* orderBox = nullptr;
	FlowLayout* feedforwardFlow = nullptr;
	FlowLayout* feedbackFlow = nullptr;
	QVector<ValueScrubBox*> feedforwardBoxes;
	QVector<ValueScrubBox*> feedbackBoxes;
	// b0..bN and a0..aN, each order + 1 long (the config-line halves).
	std::vector<double> feedforward;
	std::vector<double> feedback;
	unsigned order = 1;
	bool updating = false;
};
