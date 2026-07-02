#include "CommandRowFrame.h"

#include <QPainter>

#include "Editor/SkinManager.h"

CommandRowFrame::CommandRowFrame(QWidget* parent)
	: QFrame(parent)
{
}

void CommandRowFrame::setRowInfo(const CommandRowInfo& rowInfo)
{
	info = rowInfo;
}

const CommandRowInfo& CommandRowFrame::rowInfo() const
{
	return info;
}

void CommandRowFrame::paintEvent(QPaintEvent* event)
{
	// QFrame::paintEvent renders the stylesheet background/border exactly as
	// the plain QFrame did before this subclass existed; the skin hook then
	// draws on top (no-op in the neutral default).
	QFrame::paintEvent(event);

	// The hover flag is sampled at paint time instead of being pushed through
	// setRowInfo: hover repaints are triggered by the skin's own :hover QSS
	// rules (or the gallery's WA_UnderMouse equivalent), so the stored info
	// would always lag one state behind.
	CommandRowInfo paintInfo = info;
	paintInfo.hovered = underMouse();

	QPainter painter(this);
	SkinManager::instance()->paintCardChrome(painter, rect(), paintInfo);
}
