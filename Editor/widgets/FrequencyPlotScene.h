/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <vector>
#include <QColor>
#include <QGraphicsScene>

#include "FrequencyPlotItem.h"

// Optional token-driven colour set for the frequency plot stack (view
// background grid, response curve, node handles, rulers). tokenDriven stays
// false by default, in which case every consumer keeps its original
// hardcoded dark/light colours - the frozen legacy GraphicEQ GUI never sets
// this, so the heritage presentation is untouched. The modern GraphicEQ card
// editor fills it from SkinTokens on every skin change.
struct FrequencyPlotColors
{
	bool tokenDriven = false;
	QColor grid;
	QColor curve;
	QColor node;
	QColor nodeInk;
	QColor nodeSelected;
	QColor nodeSelectedInk;
	QColor rulerInk;
	QColor rulerCursorInk;
	QColor rulerCursorBox;
	QColor rulerCursorFrame;
};

class FrequencyPlotScene : public QGraphicsScene
{
	Q_OBJECT
public:
	explicit FrequencyPlotScene(QObject* parent = 0);

	const FrequencyPlotColors& plotColors() const;
	void setPlotColors(const FrequencyPlotColors& value);

	void addItem(FrequencyPlotItem* item);

	double xToHz(double x);
	double hzToX(double hz);
	double yToDb(double y);
	double dbToY(double db);

	double getZoomX() const;
	double getZoomY() const;
	void setZoom(double zoomX, double zoomY);

	int getBandCount() const;
	virtual void setBandCount(int value);

	const std::vector<double>& getBands();
	static const std::vector<double>& getBands(int count);

protected:
	static std::vector<double> bands15;
	static std::vector<double> bands31;
	static std::vector<double> bandsVar;

private:
	void updateSceneRect();

	double zoomX;
	double zoomY;
	int bandCount = -1;
	FrequencyPlotColors colors;
};
