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
#include <string>
#include <vector>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/State.h"
#include "filters/CopyFilter.h"

// The signal-free core of the subwoofer routing editor state: mutation
// setters, dirty tracking, validation refresh and the headroom trim readout.
// SubwooferRoutingUiModel wraps this in a QObject and turns the boolean
// "mutated" results into signals.
//
// Split out in audit #275 (B3/TD-30): EditorLogicTests has no moc step, so a
// Q_OBJECT model silently falls outside the widget-free test seam - this
// class is exactly the layer that seam targets, and it was the largest
// untested UI model in the tree while the logic lived inside the QObject.
// The convention itself is documented in Tests/EditorLogicTests/
// EditorLogicTests.vcxproj's source-list comment.
class SubwooferRoutingUiState
{
public:
	SubwooferRoutingUiState(
		const subroute::SubwooferRoutingState& state,
		unsigned deviceSampleRate);

	const subroute::SubwooferRoutingState& state() const;
	const subroute::ValidationResult& validation() const;
	unsigned sampleRate() const;
	bool isDirty() const;
	std::optional<double> computedTrimDb() const;

	// Every mutating method answers whether it changed the state (and
	// therefore refreshed validation and marked the model dirty); the QObject
	// wrapper emits its signals exactly when this is true.
	bool setSourceLfeGainDb(double gainDb);
	bool setSourceLfePolarity(bool inverted);
	bool setSourceLfeDelayMs(double milliseconds);
	bool setGroupHighPass(const std::string& groupId, double frequencyHz);
	bool setBassPathLowPass(const std::string& pathId, double frequencyHz);
	// Crossover recipes (BW/LR alignment x order) rewrite the whole section
	// run; the frequency setters above keep custom chains intact and only
	// move the corner.
	bool setGroupCrossover(
		const std::string& groupId,
		const subroute::CrossoverRecipe& recipe);
	bool setBassPathCrossover(
		const std::string& pathId,
		const subroute::CrossoverRecipe& recipe);
	bool setGroupDelayMs(const std::string& groupId, double milliseconds);
	bool setPathDelayMs(const std::string& pathId, double milliseconds);
	bool setPathPolarity(const std::string& pathId, bool inverted);
	bool setHeadroomAuto(bool automatic);
	bool setManualTrimDb(double trimDb);
	bool applyBassSendAssignments(const std::vector<Assignment>& assignments);
	bool applyOutputAssignments(const std::vector<Assignment>& assignments);
	bool replaceState(const subroute::SubwooferRoutingState& state);

private:
	bool commitMutation();
	void refreshValidation();

	subroute::SubwooferRoutingState currentState;
	subroute::ValidationResult currentValidation;
	unsigned deviceSampleRate = 0;
	bool dirty = false;
	std::optional<double> appliedTrimDb;
};
