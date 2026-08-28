/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Constitution: docs/skins/studio.md

#include "StudioSkin.h"

#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/skins/studio/cards/StudioReferenceCardView.h"
#include "Editor/skins/studio/cards/StudioSubwooferRoutingCardView.h"
#include "Editor/skins/studio/picker/StudioFilterPicker.h"
#include "Editor/skins/studio/routing/LightTraceRoutingRenderer.h"

QString StudioSkin::id() const { return QStringLiteral("studio"); }

IRoutingRenderer* StudioSkin::routingRenderer() const
{
	static LightTraceRoutingRenderer renderer;
	return &renderer;
}

FilterPickerView* StudioSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	return new StudioFilterPickerView(tokens, parent);
}

ReferenceCardView* StudioSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return new StudioReferenceCardView(kind, parent);
}

SubwooferRoutingCardView* StudioSkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	return new StudioSubwooferRoutingCardView(tokens, parent);
}

ISkin* studioSkin()
{
	static StudioSkin instance;
	return &instance;
}
