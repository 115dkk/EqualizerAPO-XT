/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix device selector: a cyberpunk operator's target list -
	devices are nodes on the board, selection acquires them. (Phase 1 stub:
	neutral base forms; the hacker instrument lands in the skin pass.)
*/

#include "DeviceSkinPainter.h"

#include "Editor/skins/SkinPaint.h"

namespace
{
class MatrixDeviceSkin : public DeviceSkinPainter
{
};
}

const DeviceSkinPainter* matrixDeviceSkinPainter()
{
	static MatrixDeviceSkin painter;
	return &painter;
}
