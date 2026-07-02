/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/soft.md for the constitution this
	answers to.

	The reference is an iOS-Settings-style row entity: a rounded-square
	pastel colour tile carrying a per-kind monogram (the picker's tile
	grammar) leads a two-line identity - the name in body ink over a friendly
	location caption in the body face, never monospace (the two-line row is
	the privilege only this constitution grants). Measured IR facts ride in
	pastel stadium chips with the deep warm chip ink. A broken reference
	stays calm: the tile shifts to a pastel warning/danger tint with a
	stroke-drawn alert mark, the caption gently shows the reference as
	written, and the host's "Locate..." label turns the Browse button into
	the accent stadium pill that owns the recovery (the missing-fonts flow,
	sized for this skin). All pastels are mixed from tokens; elevation is the
	constitutional two value steps plus a light 1px border, never a shadow.
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

	void addActionButton(ActionRole role, QAbstractButton* button) override;
	void addLeadingWidget(QWidget* widget) override;

protected:
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
	QAbstractButton* browseButton = nullptr;
	// Inline style blocks precomputed from the tokens at construction (rows
	// are recreated on every skin/theme switch, so this stays current).
	QString chipStyle;
	QString locatePillStyle;
};
