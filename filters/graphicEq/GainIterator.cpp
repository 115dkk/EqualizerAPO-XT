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

#include "stdafx.h"
#include <algorithm>

#include "filters/graphicEq/GainIterator.h"

using std::log;
using std::vector;

GainIterator::GainIterator(const vector<FilterNode>& nodes)
	: nodes(nodes), nodeLeft(nullptr), nodeRight(nullptr), logLeft(0.0), logRightMinusLeft(0.0)
{
}

double GainIterator::gainAt(double freq)
{
	if (nodeLeft == nullptr && nodeRight == nullptr || nodeLeft != nullptr && freq < nodeLeft->freq)
	{
		FilterNode findNode(freq, 0);
		vector<FilterNode>::const_iterator it = lower_bound(nodes.begin(), nodes.end(), findNode);
		if (it != nodes.begin())
			nodeLeft = &*(it - 1);
		if (it != nodes.end())
			nodeRight = &*it;

		if (nodeLeft != nullptr && nodeRight != nullptr)
		{
			logLeft = log(nodeLeft->freq);
			logRightMinusLeft = log(nodeRight->freq) - logLeft;
		}
	}
	else if (nodeRight != nullptr && freq > nodeRight->freq)
	{
		vector<FilterNode>::const_iterator it = nodes.begin() + (nodeRight - &*nodes.begin());
		while (it != nodes.end() && freq > it->freq)
		{
			it++;
		}
		if (it == nodes.end())
			nodeRight = nullptr;
		else
			nodeRight = &*it;

		if (it != nodes.begin())
			nodeLeft = &*(it - 1);

		if (nodeLeft != nullptr && nodeRight != nullptr)
		{
			logLeft = log(nodeLeft->freq);
			logRightMinusLeft = log(nodeRight->freq) - logLeft;
		}
	}

	double dbGain;
	if (nodeLeft == nullptr)
	{
		if (nodeRight == nullptr)
			dbGain = 0.0;
		else
			dbGain = nodeRight->dbGain;
	}
	else if (nodeRight == nullptr)
	{
		dbGain = nodeLeft->dbGain;
	}
	else
	{
		double t = (log(freq) - logLeft) / logRightMinusLeft;
		// to support dbGain == -INF for both nodes
		if (nodeLeft->dbGain == nodeRight->dbGain)
			dbGain = nodeLeft->dbGain;
		else
			dbGain = nodeLeft->dbGain + t * (nodeRight->dbGain - nodeLeft->dbGain);
	}

	return dbGain;
}
