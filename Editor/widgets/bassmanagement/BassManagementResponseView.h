/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <optional>

#include <QPolygonF>
#include <QString>
#include <QVector>
#include <QWidget>

#include "BassManagement/State.h"

class BassManagementUiModel;
class QPaintEvent;

class BassManagementResponseView : public QWidget
{
	Q_OBJECT

public:
	explicit BassManagementResponseView(
		BassManagementUiModel* model,
		QWidget* parent = nullptr);

	QSize sizeHint() const override;

public slots:
	void recompute();

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	struct ResponseCurve
	{
		QString id;
		bassmgmt::PathKind kind = bassmgmt::PathKind::Main;
		QVector<QPointF> samples;
	};

	QRectF plotRect() const;
	double frequencyToX(double frequencyHz) const;
	double decibelToY(double decibels) const;

	BassManagementUiModel* model = nullptr;
	QVector<ResponseCurve> curves;
	std::optional<double> appliedTrimDb;
	double minimumDb = -72.0;
	double maximumDb = 12.0;
};
