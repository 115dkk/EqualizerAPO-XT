/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"
#include "SkinSupport.h"

namespace Skins
{
QList<ISkin*> all()
{
	return { studioSkin(), minimalSkin(), softSkin(), rackSkin(), matrixSkin() };
}

ISkin* byId(const QString& id)
{
	QString resolved = id;
	if (resolved == QStringLiteral("glassy"))
		resolved = QStringLiteral("studio");
	else if (resolved == QStringLiteral("industrial"))
		resolved = QStringLiteral("rack");

	for (ISkin* skin : all())
		if (skin->id() == resolved)
			return skin;
	return studioSkin();
}
}
