/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Signal Matrix's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): the reference as a feed line of board cells -
	marker cell, location readout, payload name, boxed fact cells.
	Constitution: docs/skins/matrix.md ("참조 카드" section).
*/

#pragma once

#include <QList>

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;

class MatrixReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit MatrixReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;
	// Told by MatrixSkin::prepareCommandRow that this view is the editor of
	// a Modern row (CommandRowInfo::inlineRow). One-way: a skin or mode
	// switch rebuilds the row.
	void setInlineRow(bool inlineRow);

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	QString cardKind;
	bool rowMode = false;
	QVBoxLayout* root = nullptr;
	QHBoxLayout* feedLayout = nullptr;
	// A row's second line (setInlineRow): the location and the bus strip.
	QHBoxLayout* wiringLayout = nullptr;
	QWidget* portStrip = nullptr;
	QWidget* busStrip = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QHBoxLayout* readoutLayout = nullptr;
	QWidget* readoutStrip = nullptr;
	QLabel* markerCell = nullptr;
	ElidedLabel* nameCell = nullptr;
	ElidedLabel* locationCell = nullptr;
	QLabel* absCell = nullptr;
	QLabel* formatCell = nullptr;
	// The port strip's centre engraving; on a VST feed it also posts the
	// loaded ABI ("EXTERNAL DEVICE · VST3").
	QLabel* deviceLabel = nullptr;
	QLabel* statusLine = nullptr;
	QList<QLabel*> readoutCells;
};
