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

#include <QObject>

#include "SubwooferRoutingUiState.h"

// QObject shell around SubwooferRoutingUiState (audit #275 B3/TD-30): the
// widgets connect to the two signals here, and every piece of behavior lives
// in the signal-free core, where EditorLogicTests can reach it (the test
// project has no moc step, so anything behind Q_OBJECT is outside the seam).
class SubwooferRoutingUiModel : public QObject
{
	Q_OBJECT

public:
	explicit SubwooferRoutingUiModel(
		const subroute::SubwooferRoutingState& state,
		unsigned deviceSampleRate,
		QObject* parent = nullptr);

	const subroute::SubwooferRoutingState& state() const {return core.state();}
	const subroute::ValidationResult& validation() const {return core.validation();}
	unsigned sampleRate() const {return core.sampleRate();}
	bool isDirty() const {return core.isDirty();}
	std::optional<double> computedTrimDb() const {return core.computedTrimDb();}

	void setSourceLfeGainDb(double gainDb) {relay(core.setSourceLfeGainDb(gainDb));}
	void setSourceLfePolarity(bool inverted) {relay(core.setSourceLfePolarity(inverted));}
	void setSourceLfeDelayMs(double milliseconds) {relay(core.setSourceLfeDelayMs(milliseconds));}
	void setGroupHighPass(const std::string& groupId, double frequencyHz)
	{
		relay(core.setGroupHighPass(groupId, frequencyHz));
	}
	void setBassPathLowPass(const std::string& pathId, double frequencyHz)
	{
		relay(core.setBassPathLowPass(pathId, frequencyHz));
	}
	void setGroupCrossover(const std::string& groupId, const subroute::CrossoverRecipe& recipe)
	{
		relay(core.setGroupCrossover(groupId, recipe));
	}
	void setBassPathCrossover(const std::string& pathId, const subroute::CrossoverRecipe& recipe)
	{
		relay(core.setBassPathCrossover(pathId, recipe));
	}
	void setGroupDelayMs(const std::string& groupId, double milliseconds)
	{
		relay(core.setGroupDelayMs(groupId, milliseconds));
	}
	void setPathDelayMs(const std::string& pathId, double milliseconds)
	{
		relay(core.setPathDelayMs(pathId, milliseconds));
	}
	void setPathPolarity(const std::string& pathId, bool inverted)
	{
		relay(core.setPathPolarity(pathId, inverted));
	}
	void setHeadroomAuto(bool automatic) {relay(core.setHeadroomAuto(automatic));}
	void setManualTrimDb(double trimDb) {relay(core.setManualTrimDb(trimDb));}
	void applyBassSendAssignments(const std::vector<Assignment>& assignments)
	{
		relay(core.applyBassSendAssignments(assignments));
	}
	void applyOutputAssignments(const std::vector<Assignment>& assignments)
	{
		relay(core.applyOutputAssignments(assignments));
	}
	void replaceState(const subroute::SubwooferRoutingState& state)
	{
		relay(core.replaceState(state));
	}

signals:
	void stateEdited();
	void validationChanged();

private:
	// A mutation that changed the core state becomes the signal pair the
	// widgets listen for; a rejected/no-op mutation stays silent, as before.
	void relay(bool mutated)
	{
		if (!mutated)
			return;
		emit stateEdited();
		emit validationChanged();
	}

	SubwooferRoutingUiState core;
};
