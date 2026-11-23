/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "DeviceCaption.h"

#include <QColor>
#include <QDialog>

#include "Editor/widgets/DialogChrome.h"
#include "Editor/widgets/TitleBarSkin.h"
#include "skins/DeviceSkinPainter.h"

namespace
{
// The caption's view of the dialog-wide theme main() set
// (DeviceSkinPainter::setActiveTheme): the skin's ink for the close glyph
// and the skin's painted chrome, which the Studio, Rack and Matrix painters
// take from the same functions the Editor's skins use.
class DeviceTitleBarSkin : public TitleBarSkin
{
public:
	using TitleBarSkin::TitleBarSkin;

	QColor ink() const override
	{
		return QColor(DeviceSkinPainter::activeTokens().text);
	}

	void paintChrome(QPainter& painter, const QRect& rect) const override
	{
		DeviceSkinPainter::active()->paintTitleBarChrome(painter, rect, DeviceSkinPainter::activeTokens());
	}
};

bool mountingEnabled = false;
}

void DeviceCaption::enable(bool on)
{
	static DeviceTitleBarSkin skin;
	TitleBarSkin::install(&skin);
	mountingEnabled = on;
}

bool DeviceCaption::enabled()
{
	return mountingEnabled;
}

void DeviceCaption::mount(QDialog* dialog)
{
	if (mountingEnabled)
		DialogChrome::attach(dialog);
}
