/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Precision Minimal device selector: a terminal's boot-time device menu.
	(Phase 1 stub: neutral base forms; the terminal instrument lands in the
	skin pass.)
*/

#include "DeviceSkinPainter.h"

#include "Editor/skins/SkinPaint.h"

namespace
{
class MinimalDeviceSkin : public DeviceSkinPainter
{
};
}

const DeviceSkinPainter* minimalDeviceSkinPainter()
{
	static MinimalDeviceSkin painter;
	return &painter;
}
