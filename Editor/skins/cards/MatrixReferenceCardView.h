/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): the reference is a feed line on the board. A mono
	marker cell designates the feed ("> SRC" / "> IR" / "> IR+" / "> DEV") and
	turns into a danger readout while the reference is broken (danger is the
	documented ink for a broken include readout); the payload name is the
	brightest mono ink on the line; the location is a muted mono readout;
	measured facts sit in boxed sunken mono cells (rule 5: authoritative
	numbers live in boxed cells); the VST body is headed by the monochrome
	"> IN ... EXTERNAL DEVICE ... OUT >" port strip so the plugin reads as
	outboard gear patched into the signal path. See docs/skins/matrix.md for
	the constitution this answers to.
*/

#pragma once

#include <QList>

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;

class MatrixReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit MatrixReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addActionButton(ActionRole role, QAbstractButton* button) override;
	void addLeadingWidget(QWidget* widget) override;

protected:
	void applyState(const ReferenceCardState& state) override;

private:
	QString cardKind;
	QHBoxLayout* feedLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QHBoxLayout* readoutLayout = nullptr;
	QWidget* readoutStrip = nullptr;
	QLabel* markerCell = nullptr;
	ElidedLabel* nameCell = nullptr;
	ElidedLabel* locationCell = nullptr;
	QLabel* absCell = nullptr;
	QLabel* formatCell = nullptr;
	QLabel* statusLine = nullptr;
	QAbstractButton* browseButton = nullptr;
	QList<QLabel*> readoutCells;
};
