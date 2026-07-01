/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/matrix.md for the constitution this
	answers to.
*/

#pragma once

#include "Editor/widgets/cards/DefaultReferenceCardView.h"

class MatrixReferenceCardView : public DefaultReferenceCardView
{
	Q_OBJECT

public:
	explicit MatrixReferenceCardView(const QString& kind, QWidget* parent = nullptr);
};
