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

#include "BassManagement/State.h"
#include "filters/CopyFilter.h"

class BassManagementRoutingAdapter
{
public:
	static std::vector<Assignment> toBassSendAssignments(
		const bassmgmt::BassManagementState& state);
	static QStringList bassSendSources(
		const bassmgmt::BassManagementState& state);
	static void applyBassSendAssignments(
		bassmgmt::BassManagementState& state,
		const std::vector<Assignment>& assignments);

	static std::vector<Assignment> toOutputAssignments(
		const bassmgmt::BassManagementState& state);
	static QStringList outputSources(
		const bassmgmt::BassManagementState& state);
	static void applyOutputAssignments(
		bassmgmt::BassManagementState& state,
		const std::vector<Assignment>& assignments);
};
