/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#pragma once

#include <QFont>
#include <QString>
#include <QWidget>

#include "Editor/widgets/cards/SubwooferRoutingCardView.h"

class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class RackElidingLabel;

// One engraved crossover instrument: the caption over up to two readout
// lines (HP and LP, each with its recognized alignment label). Review
// round 3 folded the two half-empty per-type meters into this one.
class RackCrossoverReadout : public QWidget
{
	Q_OBJECT

public:
	explicit RackCrossoverReadout(QWidget* parent = nullptr);

	void setReadout(const QString& newCaption,
		const QString& newPrimary,
		const QString& newSecondary = QString());

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QFont captionFont() const;
	QFont valueFont() const;

	QString caption;
	QString primary;
	QString secondary;
};

class RackLfeLamp : public QWidget
{
	Q_OBJECT

public:
	explicit RackLfeLamp(QWidget* parent = nullptr);

	void setLfeState(bool newPreserved, double newGainDb);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QFont captionFont() const;
	QFont valueFont() const;

	bool preserved = false;
	double gainDb = 0.0;
};

class RackHeadroomMeter : public QWidget
{
	Q_OBJECT

public:
	explicit RackHeadroomMeter(QWidget* parent = nullptr);

	void setHeadroom(bool newAutomatic, double newTrimDb);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QFont captionFont() const;
	QFont scaleFont() const;

	bool automatic = true;
	double trimDb = 0.0;
	bool trimFinite = true;
};

class RackSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit RackSubwooferRoutingCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateResponsiveLayout();

	QWidget* headerWidget = nullptr;
	QWidget* instrumentWidget = nullptr;
	QWidget* actionHost = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* validityLabel = nullptr;
	RackElidingLabel* layoutLabel = nullptr;
	RackElidingLabel* profileLabel = nullptr;
	QLabel* statusLabel = nullptr;
	RackCrossoverReadout* crossoverReadout = nullptr;
	RackLfeLamp* lfeLamp = nullptr;
	RackHeadroomMeter* headroomMeter = nullptr;
};
