/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include "Editor/skins/Skins.h"
#include "Editor/skins/rack/cards/RackReferenceCardView.h"
#include "Editor/skins/rack/cards/RackSubwooferRoutingCardView.h"
#include "Editor/skins/rack/picker/RackFilterPicker.h"
#include "Editor/skins/rack/routing/HardwarePatchbayRoutingRenderer.h"

QString RackSkin::id() const
{
	return QStringLiteral("rack");
}

IRoutingRenderer* RackSkin::routingRenderer() const
{
	static HardwarePatchbayRoutingRenderer renderer;
	return &renderer;
}

FilterPickerView* RackSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	return new RackFilterPickerView(tokens, parent);
}

ReferenceCardView* RackSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	return new RackReferenceCardView(kind, tokens, parent);
}

SubwooferRoutingCardView* RackSkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	return new RackSubwooferRoutingCardView(tokens, parent);
}

ISkin* rackSkin()
{
	static RackSkin instance;
	return &instance;
}
