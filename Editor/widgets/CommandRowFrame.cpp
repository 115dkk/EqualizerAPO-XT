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

	QPainter painter(this);
	// The hover flag is paint-time state, not row state, so it is filled here
	// instead of by the owning row's refreshStateProperties. Repaints on
	// enter/leave come for free once the active skin's frame stylesheet
	// contains a :hover rule (the stylesheet style enables hover tracking).
	CommandRowInfo paintInfo = info;
	paintInfo.hovered = underMouse();
	SkinManager::instance()->paintCardChrome(painter, rect(), paintInfo);
}
