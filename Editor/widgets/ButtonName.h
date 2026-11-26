/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The name a button without words is given for assistive technology. Qt
	hands a screen reader a button's accessible name and, when there is none,
	the button's text. A button whose face is an icon is then announced with
	no name at all, and one whose face is a sign as that sign ("v", ">", "+").
	Such a button already says what it does in its tooltip, so the tooltip is
	its name too. The gallery checks it in every capture (checkButtonNames,
	GallerySelfTests.cpp).
*/

#pragma once

#include <QAbstractButton>
#include <QString>
#include <QVariant>

#include <algorithm>

// True when a label can stand as a name on its own: it has at least two
// letters or digits. "+", ">", "..." and the "v" that draws a fold arrow do
// not; "LOCATE" and "Locate..." do.
inline bool buttonLabelHasWords(const QString& label)
{
	return std::count_if(label.cbegin(), label.cend(), [](QChar c) { return c.isLetterOrNumber(); }) >= 2;
}

// Gives a button without a worded label its tooltip as its accessible name.
// A button with a worded label is left to be named by that label, which is
// what voice control matches against, and so is a button that was given a
// name by hand. Call it after the button's text and tooltip are set, and
// again when either changes: a button that gained a label loses the name
// this function gave it.
inline void nameButtonByToolTip(QAbstractButton* button)
{
	if (button == nullptr)
		return;

	static const char fromToolTip[] = "accessibleNameFromToolTip";
	const bool ours = button->property(fromToolTip).toBool();
	if (!button->accessibleName().isEmpty() && !ours)
		return;

	if (!buttonLabelHasWords(button->text()))
	{
		button->setAccessibleName(button->toolTip());
		button->setProperty(fromToolTip, true);
	}
	else if (ours)
	{
		button->setAccessibleName(QString());
		button->setProperty(fromToolTip, false);
	}
}
