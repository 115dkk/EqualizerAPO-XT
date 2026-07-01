/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/soft.md for the constitution this
	answers to.
*/

#pragma once

#include "Editor/widgets/cards/DefaultReferenceCardView.h"

class SoftReferenceCardView : public DefaultReferenceCardView
{
	Q_OBJECT

public:
	explicit SoftReferenceCardView(const QString& kind, QWidget* parent = nullptr);
};
