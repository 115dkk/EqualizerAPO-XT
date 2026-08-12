/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Precision Minimal's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): one line of monospace type, reading

	  [channel] payload MISSING VST3 ABS location readout ! status ... BROWSE EDIT

	Constitution: docs/skins/minimal.md ("참조 카드" section).

	Colours live in precision_dark.qss / precision_light.qss (historic names);
	this class only builds structure and sets state-carrying properties. Long
	strings elide at paint time (ElidedLabel), never at set time.
*/

#pragma once

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;

class MinimalReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit MinimalReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	QHBoxLayout* lineLayout = nullptr;
	ElidedLabel* nameLabel = nullptr;
	QLabel* missingToken = nullptr;
	QLabel* formatToken = nullptr;
	QLabel* absToken = nullptr;
	ElidedLabel* dirLabel = nullptr;
	QLabel* readoutLabel = nullptr;
	ElidedLabel* statusLabel = nullptr;
	// Insertion cursor for leading widgets (MultiConvolution's channel combo
	// sits at the line head, before the payload).
	int leadingIndex = 0;
};
