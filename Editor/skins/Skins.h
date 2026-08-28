/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Registry of the built-in skins. SkinManager looks skins up here by id.
*/

#pragma once

#include <QList>

#include "ISkin.h"

namespace Skins
{
// All registered skins, in display order. Instances are owned statically.
QList<ISkin*> all();

// Skin for the given id, applying legacy aliases (glassy->studio,
// industrial->rack). Falls back to the studio skin for unknown ids.
ISkin* byId(const QString& id);
}
