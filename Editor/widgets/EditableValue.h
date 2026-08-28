/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QStackedLayout>
#include <QWidget>

class EditableValue : public QWidget
{
	Q_OBJECT

public:
	explicit EditableValue(QWidget* parent = nullptr);

	double value() const;
	void setValue(double value);
	const QString& unit() const;
	void setUnit(const QString& unit);
	// Display precision. The default of 1 is the Preamp card's look; the
	// Delay card needs 2 in milliseconds mode (0.25 ms must not read as
	// 0.3 ms) and 0 for whole samples.
	void setDecimals(int decimals);

signals:
	void valueChanged(double value);

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
	void commitEdit();

private:
	void refreshText();

	QStackedLayout* stack = nullptr;
	QLabel* displayLabel = nullptr;
	QLineEdit* editField = nullptr;
	double currentValue = 0.0;
	QString currentUnit;
	int displayDecimals = 1;
};
