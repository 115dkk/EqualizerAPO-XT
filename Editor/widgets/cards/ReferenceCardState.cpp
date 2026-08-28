/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "ReferenceCardView.h"

#include <QDir>

QString ReferenceCardState::locationPrefix() const
{
	if (directory.isEmpty())
		return QString();
	if (directory.endsWith(QLatin1Char('\\')) || directory.endsWith(QLatin1Char('/')))
		return directory;
	return directory + QDir::separator();
}

bool referenceCardNeedsLocate(const ReferenceCardState& state)
{
	return state.missing && !state.editText.trimmed().isEmpty();
}

QString referenceCardSeverityName(ReferenceCardState::Severity severity)
{
	switch (severity)
	{
	case ReferenceCardState::Severity::Critical:
		return QStringLiteral("critical");
	case ReferenceCardState::Severity::Warning:
		return QStringLiteral("warning");
	case ReferenceCardState::Severity::None:
	default:
		return QStringLiteral("none");
	}
}
