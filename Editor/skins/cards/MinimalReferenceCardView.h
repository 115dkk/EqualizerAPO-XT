/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Precision Minimal's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/minimal.md for the constitution this
	answers to.
*/

#pragma once

#include "Editor/widgets/cards/DefaultReferenceCardView.h"

class MinimalReferenceCardView : public DefaultReferenceCardView
{
	Q_OBJECT

public:
	explicit MinimalReferenceCardView(const QString& kind, QWidget* parent = nullptr);
};
