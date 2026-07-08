#include "AddCardRow.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

AddCardRow::AddCardRow(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("FilterAddRow"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Add filter"));
	setAccessibleName(tr("Add filter"));
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		updateGeometry();
		update();
	});
}

QSize AddCardRow::sizeHint() const
{
	// Mirror a card row's footprint: rowHeight plus the 4px outer margins the
	// cards carry above and below, so the ghost row sits on the same rhythm.
	return QSize(200, SkinManager::instance()->tokens().rowHeight + 8);
}

QSize AddCardRow::minimumSizeHint() const
{
	return QSize(0, SkinManager::instance()->tokens().rowHeight + 8);
}

void AddCardRow::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	ListChromeState state;
	state.hovered = hovered;
	state.pressed = pressed;
	state.focused = hasFocus();
	state.label = tr("Add filter");
	// The same horizontal inset as the card rows' outer layout (8px), so the
	// skin paints on the exact column the cards occupy.
	SkinManager::instance()->paintAddRow(painter, rect().adjusted(8, 4, -8, -4), state);
}

void AddCardRow::enterEvent(QEnterEvent*)
{
	hovered = true;
	update();
}

void AddCardRow::leaveEvent(QEvent*)
{
	hovered = false;
	pressed = false;
	update();
}

void AddCardRow::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}
	pressed = true;
	update();
}

void AddCardRow::mouseReleaseEvent(QMouseEvent* event)
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

void AddCardRow::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
	{
		emit activated();
		return;
	}
	QWidget::keyPressEvent(event);
}

void AddCardRow::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void AddCardRow::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}
