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

#include <cmath>
#include <complex>

#include "AnalysisPlotScene.h"

using std::log10;
using std::sqrt;
using std::vector;

AnalysisPlotScene::AnalysisPlotScene(QObject* parent)
	: FrequencyPlotScene(parent)
{
}

// Magnitude-only dB nodes, derived from the same complex bins the rest of the
// analysis now works from. The bin count stops one short of the response's own
// (fftSize / 2 rather than fftSize / 2 + 1) because that is what this scene has
// always plotted; matching it exactly keeps the magnitude curve identical
// through the change of data source.
void AnalysisPlotScene::setResponse(const AnalysisResponse& response)
{
	const size_t count = response.fftSize / 2;
	nodes.clear();
	nodes.reserve(count);
	for (size_t i = 0; i < count && i < response.bins.size(); i++)
	{
		double freq = response.frequencyOf(i);
		// GainIterator can't handle 0 Hz node
		if (freq == 0.0)
			freq = 0.001;
		// sqrt(re^2 + im^2) rather than std::abs(complex): std::abs guards
		// against intermediate overflow and can land a bit away from this
		// expression. That difference is far below anything visible, but the
		// curve builder that replaces this path has to be shown to reproduce
		// the magnitude exactly, and a last-bit disagreement between two
		// spellings of the same formula would make that comparison useless.
		const double re = response.bins[i].real();
		const double im = response.bins[i].imag();
		double dbGain = log10(sqrt(re * re + im * im)) * 20;
		nodes.emplace_back(freq, dbGain);
	}

	update();
}

const vector<FilterNode>& AnalysisPlotScene::getNodes() const
{
	return nodes;
}
