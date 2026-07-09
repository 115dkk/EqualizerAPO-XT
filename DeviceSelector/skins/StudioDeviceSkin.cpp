/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass device selector: the device list as a glowing glass console.
	(Phase 1 stub: neutral base forms; the studio instrument lands in the
	skin pass.)
*/

#include "DeviceSkinPainter.h"

#include "Editor/skins/SkinPaint.h"

namespace
{
class StudioDeviceSkin : public DeviceSkinPainter
{
};
}

const DeviceSkinPainter* studioDeviceSkinPainter()
{
	static StudioDeviceSkin painter;
	return &painter;
}
