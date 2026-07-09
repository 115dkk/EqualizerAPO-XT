/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab device selector: fear-free device cards - every press looks
	safe, every consequence is spelled out. (Phase 1 stub: neutral base
	forms; the friendly instrument lands in the skin pass.)
*/

#include "DeviceSkinPainter.h"

#include "Editor/skins/SkinPaint.h"

namespace
{
class SoftDeviceSkin : public DeviceSkinPainter
{
};
}

const DeviceSkinPainter* softDeviceSkinPainter()
{
	static SoftDeviceSkin painter;
	return &painter;
}
