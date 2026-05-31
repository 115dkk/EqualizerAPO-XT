/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A skin is a self-contained visual identity. Beyond colour tokens it decides
	which QSS sheet to load and which Copy routing renderer to inject, so that
	each skin can present the same configuration with a genuinely different
	philosophy rather than only different colours. SkinManager owns the active
	ISkin and delegates to it; new skins are added by implementing this
	interface and registering an instance (see Skins.cpp).
*/

#pragma once

#include <QString>

#include "Editor/SkinTokens.h"

class IRoutingRenderer;

class ISkin
{
public:
	virtual ~ISkin() = default;

	// Stable identifier persisted in settings (e.g. "studio", "matrix").
	virtual QString id() const = 0;

	// Colour + metric tokens for the requested mode.
	virtual SkinTokens tokens(bool dark) const = 0;

	// Resource path of the QSS sheet for the requested mode.
	virtual QString qssResource(bool dark) const = 0;

	// The Copy routing renderer that matches this skin's philosophy. May be
	// nullptr, in which case the caller falls back to the legacy CopyFilterGUI.
	virtual IRoutingRenderer* routingRenderer() const = 0;
};
