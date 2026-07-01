/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/studio.md for the constitution this
	answers to.
*/

#pragma once

#include "Editor/widgets/cards/DefaultReferenceCardView.h"

class StudioReferenceCardView : public DefaultReferenceCardView
{
	Q_OBJECT

public:
	explicit StudioReferenceCardView(const QString& kind, QWidget* parent = nullptr);
};
