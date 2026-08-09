/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include "Editor/skins/shared/SkinFileIcons.h"

class StudioFileIconProvider final : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override;
};
