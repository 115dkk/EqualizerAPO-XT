/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): a pastel tile leading a two-line identity, with
	measured facts as stadium chips.
	Constitution: docs/skins/soft.md ("참조 카드" section).
*/

#pragma once

#include <QString>

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class SoftReferenceTile;

class SoftReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit SoftReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	void rebuildChips(const QStringList& readout);
	void styleBrowseButton();

	QString cardKind;
	QHBoxLayout* rootLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QHBoxLayout* chipLayout = nullptr;
	SoftReferenceTile* tile = nullptr;
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
