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

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/State.h"
#include "filters/CopyFilter.h"

class SubwooferRoutingUiModel : public QObject
{
	Q_OBJECT

public:
	explicit SubwooferRoutingUiModel(
		const subroute::SubwooferRoutingState& state,
		unsigned deviceSampleRate,
		QObject* parent = nullptr);

	const subroute::SubwooferRoutingState& state() const;
	const subroute::ValidationResult& validation() const;
	unsigned sampleRate() const;
	bool isDirty() const;
	std::optional<double> computedTrimDb() const;

	void setSourceLfeGainDb(double gainDb);
	void setSourceLfePolarity(bool inverted);
	void setSourceLfeDelayMs(double milliseconds);
	void setGroupHighPass(
		const std::string& groupId, double frequencyHz);
	void setBassPathLowPass(
		const std::string& pathId, double frequencyHz);
	// Crossover recipes (BW/LR alignment x order) rewrite the whole section
	// run; the frequency setters above keep custom chains intact and only
	// move the corner.
	void setGroupCrossover(
		const std::string& groupId,
		const subroute::CrossoverRecipe& recipe);
	void setBassPathCrossover(
		const std::string& pathId,
		const subroute::CrossoverRecipe& recipe);
	void setGroupDelayMs(
		const std::string& groupId, double milliseconds);
	void setPathDelayMs(
		const std::string& pathId, double milliseconds);
	void setPathPolarity(
		const std::string& pathId, bool inverted);
	void setHeadroomAuto(bool automatic);
	void setManualTrimDb(double trimDb);
	void applyBassSendAssignments(
		const std::vector<Assignment>& assignments);
	void applyOutputAssignments(
		const std::vector<Assignment>& assignments);
	void replaceState(const subroute::SubwooferRoutingState& state);

signals:
	void stateEdited();
	void validationChanged();

private:
	void commitMutation();
	void refreshValidation();

	subroute::SubwooferRoutingState currentState;
	subroute::ValidationResult currentValidation;
	unsigned deviceSampleRate = 0;
	bool dirty = false;
	std::optional<double> appliedTrimDb;
};
