/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#pragma once

#include <QFont>
#include <QString>
#include <QWidget>

#include "Editor/widgets/cards/BassManagementCardView.h"

class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;

class RackCrossoverReadout : public QWidget
{
	Q_OBJECT

public:
	explicit RackCrossoverReadout(QWidget* parent = nullptr);

	void setReadout(const QString& newCaption, const QString& newValue);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QFont captionFont() const;
	QFont valueFont() const;

	QString caption;
	QString value;
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
	bool trimAvailable = true;
};

class RackBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit RackBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateResponsiveLayout();
	void updateLabelPalettes(bool invalid, bool warning);

	QWidget* headerWidget = nullptr;
	QWidget* instrumentWidget = nullptr;
	QWidget* actionHost = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* validityLabel = nullptr;
	QLabel* layoutLabel = nullptr;
	QLabel* profileLabel = nullptr;
	QLabel* countsLabel = nullptr;
	QLabel* statusLabel = nullptr;
	RackCrossoverReadout* highPassReadout = nullptr;
	RackCrossoverReadout* lowPassReadout = nullptr;
	RackLfeLamp* lfeLamp = nullptr;
	RackHeadroomMeter* headroomMeter = nullptr;
};
