/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Hardware Rack device selector: a patch bay - every device is a jack
	field on the rack, and installing the APO patches a cable into it.
	(Phase 1 stub: neutral base forms; the skeuomorphic instrument lands in
	the skin pass.)
*/

#include "DeviceSkinPainter.h"

#include "Editor/skins/SkinPaint.h"

namespace
{
class RackDeviceSkin : public DeviceSkinPainter
{
};
}

const DeviceSkinPainter* rackDeviceSkinPainter()
{
	static RackDeviceSkin painter;
	return &painter;
}
