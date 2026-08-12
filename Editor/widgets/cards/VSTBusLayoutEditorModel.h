/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Document-side state for the ModernCards VST main-bus editor. Keeping the
	legacy migration and paired Input/Output contract outside QWidget code makes
	the config transition deterministic and independently testable.
*/

#pragma once

#include <optional>

#include "vst/VST3BusLayout.h"

class VSTBusLayoutEditorModel
{
public:
	VSTBusLayoutEditorModel(const std::optional<VST3BusContract>& contract = std::nullopt,
		bool legacyStereoInput = false);

	VST3BusLayout input() const noexcept;
	VST3BusLayout output() const noexcept;
	const std::optional<VST3BusContract>& contract() const noexcept;
	bool migratedLegacyStereoInput() const noexcept;

	void setLayouts(VST3BusLayout input, VST3BusLayout output);
	void removeLayouts();

private:
	std::optional<VST3BusContract> currentContract;
	bool migratedLegacy = false;
};
