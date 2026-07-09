/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"
#include "SkinSupport.h"
#include "SkinThemeData.h"

namespace Skins
{
QList<ISkin*> all()
{
	return { studioSkin(), minimalSkin(), softSkin(), rackSkin(), matrixSkin() };
}

ISkin* byId(const QString& id)
{
	// Alias resolution and the studio fallback live in SkinThemeData so
	// satellite tools resolve stored ids exactly like the Editor.
	const QString resolved = SkinThemeData::resolveId(id);
	for (ISkin* skin : all())
		if (skin->id() == resolved)
			return skin;
	return studioSkin();
}
}
