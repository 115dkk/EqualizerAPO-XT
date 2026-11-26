/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SubwooferRoutingCardState.h"

#include <QCoreApplication>
#include <QStringList>
#include <QtGlobal>

// The texts keep the context "SubwooferRoutingCardView" (the class that held
// them before) so the existing translations still apply.

namespace
{
QString formatHz(double hz)
{
	const QString format = QCoreApplication::translate("SubwooferRoutingCardView", "%1 Hz");
	return format.arg(QString::number(hz, 'g', 5));
}
}

namespace SubwooferRoutingCardText
{
QString crossoverSummary(const SubwooferRoutingCardState& state)
{
	const bool hasHighPass = state.highPassHz > 0.0;
	const bool hasLowPass = state.lowPassHz > 0.0;

	if (hasHighPass && hasLowPass
		&& qFuzzyCompare(state.highPassHz, state.lowPassHz))
	{
		return formatHz(state.highPassHz);
	}

	QStringList parts;
	if (hasHighPass)
	{
		const QString format = QCoreApplication::translate("SubwooferRoutingCardView", "HP %1");
		parts.append(format.arg(formatHz(state.highPassHz)));
	}
	if (hasLowPass)
	{
		const QString format = QCoreApplication::translate("SubwooferRoutingCardView", "LP %1");
		parts.append(format.arg(formatHz(state.lowPassHz)));
	}
	if (parts.isEmpty())
		return QCoreApplication::translate("SubwooferRoutingCardView", "Full range");
	return parts.join(QStringLiteral(" / "));
}
}
