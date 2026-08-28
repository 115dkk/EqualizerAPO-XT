/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Minimal skin. Constitution: docs/skins/minimal.md. The file-scope
// instance is exposed through minimalSkin() so Skins::all() can assemble
// the roster without a central definition list.

#include "MinimalSkin.h"

#include "Editor/skins/shared/SkinSupport.h"
#include "cards/MinimalReferenceCardView.h"
#include "cards/MinimalSubwooferRoutingCardView.h"
#include "picker/MinimalFilterPicker.h"
#include "routing/StepListRoutingRenderer.h"

QString MinimalSkin::id() const { return QStringLiteral("minimal"); }
IRoutingRenderer* MinimalSkin::routingRenderer() const
{
	static StepListRoutingRenderer renderer;
	return &renderer;
}
FilterPickerView* MinimalSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	// The add-filter dropdown as a numbered terminal index; see
	// MinimalFilterPicker.h for the design.
	return new MinimalFilterPickerView(tokens, parent);
}
// The reference bodies as one line of type; see
// MinimalReferenceCardView.h.
ReferenceCardView* MinimalSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return new MinimalReferenceCardView(kind, parent);
}

SubwooferRoutingCardView* MinimalSkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return new MinimalSubwooferRoutingCardView(parent);
}

ISkin* minimalSkin()
{
	static MinimalSkin instance;
	return &instance;
}
