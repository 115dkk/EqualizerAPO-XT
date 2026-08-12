/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

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
class QLabel;

class MatrixReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit MatrixReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
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
	QList<QLabel*> readoutCells;
};
