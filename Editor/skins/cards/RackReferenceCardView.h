/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Hardware Rack's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/rack.md for the constitution this
	answers to.
*/

#pragma once

#include "Editor/widgets/cards/DefaultReferenceCardView.h"

class RackReferenceCardView : public DefaultReferenceCardView
{
	Q_OBJECT

public:
	explicit RackReferenceCardView(const QString& kind, QWidget* parent = nullptr);
};
