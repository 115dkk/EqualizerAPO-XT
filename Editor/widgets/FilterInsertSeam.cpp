#include "FilterInsertSeam.h"

#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

FilterInsertSeam::FilterInsertSeam(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("FilterInsertSeam"));
	// Invisible at rest: the widget only paints while hovered, so it must not
	// contribute any background of its own.
	setAttribute(Qt::WA_NoSystemBackground, true);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Insert filter at the top"));
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

void FilterInsertSeam::paintEvent(QPaintEvent*)
{
	if (!hovered && !pressed)
		return;

	QPainter painter(this);
	ListChromeState state;
	state.hovered = hovered;
	state.pressed = pressed;
	state.label = tr("Insert filter at the top");
	SkinManager::instance()->paintInsertSeam(painter, rect().adjusted(8, 0, -8, 0), state);
}

void FilterInsertSeam::enterEvent(QEnterEvent*)
{
	hovered = true;
	update();
}

void FilterInsertSeam::leaveEvent(QEvent*)
{
	hovered = false;
	pressed = false;
	update();
}

void FilterInsertSeam::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}
	pressed = true;
	update();
}

void FilterInsertSeam::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}
	const bool wasPressed = pressed;
	pressed = false;
	update();
	if (wasPressed && rect().contains(event->pos()))
		emit activated();
}
