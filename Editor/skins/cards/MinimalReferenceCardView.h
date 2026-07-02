/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Precision Minimal's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/minimal.md for the constitution this
	answers to.

	The body is one line of monospace type - the terminal line the constitution
	asks for ("Include/VST rows are the same one-line grammar; the payload is
	brighter than the command tokens"). Reading order on the line:

	  [channel] payload MISSING VST3 ABS location readout ! status ... BROWSE EDIT

	- The payload (file / plugin name) is the brightest ink; location and the
	  measured readout follow as muted print; a host status folds into the same
	  line as an ink-tagged "!" / "!!" diagnostic instead of opening a second
	  line.
	- A broken reference is the inverted MISSING block (foreground/background
	  swap - the bluntest cursor a text instrument has), rendered as text, not
	  a coloured pill.
	- The host's action buttons are re-engraved as uppercase command tokens
	  (BROWSE/LOCATE, IMPORT, EDIT, PANEL, OPT) with a 1px underline
	  affordance; behavior, visibility and tooltips stay with the host.
	- Long strings elide at paint time (ElidedLabel), never at set time.

	Colours live in precision_dark.qss / precision_light.qss (historic names);
	this class only builds structure and sets state-carrying properties.
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

	void addActionButton(ActionRole role, QAbstractButton* button) override;
	void addLeadingWidget(QWidget* widget) override;

protected:
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
	QAbstractButton* browseButton = nullptr;
	// Insertion cursor for leading widgets (MultiConvolution's channel combo
	// sits at the line head, before the payload).
	int leadingIndex = 0;
};
