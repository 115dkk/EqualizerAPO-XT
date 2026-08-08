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

#include "SubwooferRouting/State.h"

class SubwooferRoutingUiModel;
class QPaintEvent;

class SubwooferRoutingResponseView : public QWidget
{
	Q_OBJECT

public:
	explicit SubwooferRoutingResponseView(
		SubwooferRoutingUiModel* model,
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
		subroute::PathKind kind = subroute::PathKind::Main;
		QVector<QPointF> samples;
	};

	// One placed legend entry; the strip lives above the plot so curves can
	// never cross its text, and long channel lists wrap into further rows.
	struct LegendEntry
	{
		QString id;
		subroute::PathKind kind = subroute::PathKind::Main;
		double x = 0.0;
		int row = 0;
		double textWidth = 0.0;
	};

	QRectF plotRect() const;
	void updateLegendLayout();
	double frequencyToX(double frequencyHz) const;
	double decibelToY(double decibels) const;

	SubwooferRoutingUiModel* model = nullptr;
	QVector<ResponseCurve> curves;
	QVector<LegendEntry> legendEntries;
	int legendHeightPx = 0;
	int legendRowHeightPx = 16;
	std::optional<double> appliedTrimDb;
	double minimumDb = -72.0;
	double maximumDb = 12.0;
};
