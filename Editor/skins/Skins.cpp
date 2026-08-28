/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 EqualizerAPO-XT contributors
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"
#include "shared/SkinSupport.h"
#include "SkinThemeData.h"
#include "services/logging/Logging.h"

namespace
{
// The ISkin half of the roster: which class implements each id. The membership
// and the order come from SkinThemeData::roster(), so this table only answers
// "who paints it" - and an id in the roster with no implementation here is
// reported rather than silently drawn as Studio, which is what the old
// hand-written list allowed.
ISkin* implementationFor(const QString& id)
{
	if (id == QStringLiteral("studio"))
		return studioSkin();
	if (id == QStringLiteral("minimal"))
		return minimalSkin();
	if (id == QStringLiteral("soft"))
		return softSkin();
	if (id == QStringLiteral("rack"))
		return rackSkin();
	if (id == QStringLiteral("matrix"))
		return matrixSkin();
	return nullptr;
}
}

namespace Skins
{
QList<ISkin*> all()
{
	QList<ISkin*> result;
	for (const QString& id : SkinThemeData::ids())
	{
		ISkin* skin = implementationFor(id);
		if (skin != nullptr)
			result.append(skin);
		else
			LogFStatic(L"skin \"%s\" is in the roster but has no ISkin implementation", id.toStdWString().c_str());
	}
	return result;
}

ISkin* byId(const QString& id)
{
	// Alias resolution and the studio fallback live in SkinThemeData so
	// satellite tools resolve stored ids exactly like the Editor.
	const QString resolved = SkinThemeData::resolveId(id);
	ISkin* skin = implementationFor(resolved);
	return skin != nullptr ? skin : studioSkin();
}
}
