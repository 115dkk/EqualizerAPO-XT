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

#include <vector>

#include <QStringList>

#include "SubwooferRouting/State.h"
#include "filters/CopyFilter.h"

class SubwooferRoutingRoutingAdapter
{
public:
	static std::vector<Assignment> toBassSendAssignments(
		const subroute::SubwooferRoutingState& state);
	static QStringList bassSendSources(
		const subroute::SubwooferRoutingState& state);
	static void applyBassSendAssignments(
		subroute::SubwooferRoutingState& state,
		const std::vector<Assignment>& assignments);

	static std::vector<Assignment> toOutputAssignments(
		const subroute::SubwooferRoutingState& state);
	static QStringList outputSources(
		const subroute::SubwooferRoutingState& state);
	static void applyOutputAssignments(
		subroute::SubwooferRoutingState& state,
		const std::vector<Assignment>& assignments);
};
