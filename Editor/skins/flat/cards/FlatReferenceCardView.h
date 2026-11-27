/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Stock Flat's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): a pastel tile leading a two-line identity, with
	measured facts as stadium chips.
	Constitution: docs/skins/flat.md ("참조 카드" section).
*/

#pragma once

#include <QString>

#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/SkinTokens.h"

class ElidedLabel;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class FlatReferenceTile;

class FlatReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit FlatReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

	// Called by FlatSkin::prepareCommandRow when the view sits in a Modern
	// row (CommandRowInfo::inlineRow). One-way: a skin or mode switch
	// rebuilds the row. A VST unit says itself in two lines there (name, tag
	// and buttons on the first; where it points and its bus strip on the
	// second); every kind keeps 12 characters of its name before eliding.
	void setInlineRow(bool inlineRow);

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	const SkinTokens skinTokens;
	void rebuildChips(const QStringList& readout);
	void styleBrowseButton();
	void keepNameCharacters();

	QString cardKind;
	bool rowMode = false;
	QHBoxLayout* rootLayout = nullptr;
	QWidget* textColumn = nullptr;
	QHBoxLayout* nameLayout = nullptr;
	QHBoxLayout* wiringLayout = nullptr;
	QWidget* busStrip = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QHBoxLayout* chipLayout = nullptr;
	FlatReferenceTile* tile = nullptr;
	ElidedLabel* nameLabel = nullptr;
	QLabel* formatChip = nullptr;
	ElidedLabel* captionLabel = nullptr;
	QWidget* chipRow = nullptr;
	QLabel* statusLabel = nullptr;
	// Inline style blocks precomputed from the tokens at construction (rows
	// are recreated on every skin/theme switch, so this stays current).
	QString chipStyle;
	QString locatePillStyle;
};
