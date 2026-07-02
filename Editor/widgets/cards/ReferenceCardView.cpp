#include "ReferenceCardView.h"

#include <QDir>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QStyle>

#include "Editor/SkinManager.h"

namespace
{
// Re-evaluate a widget's stylesheet after a dynamic property changed.
void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}
}

QString ReferenceCardState::locationPrefix() const
{
	if (directory.isEmpty())
		return QString();
	// Drive roots ("C:\") already end in their separator.
	if (directory.endsWith(QLatin1Char('\\')) || directory.endsWith(QLatin1Char('/')))
		return directory;
	return directory + QDir::separator();
}

ReferenceCardView::ReferenceCardView(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("ReferenceCardView"));
	setAttribute(Qt::WA_StyledBackground, true);

	stack = new QStackedLayout(this);
	stack->setContentsMargins(0, 0, 0, 0);
	// The pages differ in height (a one-line editor vs a multi-line card);
	// sizing to the current page keeps edit mode from stretching the card.
	stack->setSizeConstraint(QLayout::SetNoConstraint);

	content = new QWidget(this);
	content->setObjectName(QStringLiteral("RefCardContent"));
	content->setAttribute(Qt::WA_StyledBackground, true);
	stack->addWidget(content);

	pathEdit = new QLineEdit(this);
	pathEdit->setObjectName(QStringLiteral("RefPathEdit"));
	pathEdit->installEventFilter(this);
	connect(pathEdit, SIGNAL(editingFinished()), this, SLOT(editCommitted()));
	stack->addWidget(pathEdit);

	stack->setCurrentWidget(content);
}

void ReferenceCardView::setState(const ReferenceCardState& state)
{
	currentState = state;

	// Shared QSS surface: skins can key any child selector off the card's
	// kind and broken-reference state without each view re-implementing it.
	setProperty("refKind", state.kind);
	setProperty("refMissing", state.missing);
	applyState(currentState);
	repolish(this);

	if (nameActivationWidget != nullptr)
		nameActivationWidget->setCursor(state.nameClickable && !state.missing
			? Qt::PointingHandCursor : Qt::ArrowCursor);

	if (!pathEdit->hasFocus())
		pathEdit->setText(state.editText);
}

const ReferenceCardState& ReferenceCardView::state() const
{
	return currentState;
}

QWidget* ReferenceCardView::contentWidget() const
{
	return content;
}

void ReferenceCardView::installNameActivation(QWidget* widget)
{
	nameActivationWidget = widget;
	widget->installEventFilter(this);
}

void ReferenceCardView::enterEditMode()
{
	if (stack->currentWidget() == pathEdit)
		return;
	pathEdit->setText(currentState.editText);
	stack->setCurrentWidget(pathEdit);
	pathEdit->setFocus();
	pathEdit->selectAll();
}

void ReferenceCardView::leaveEditMode()
{
	stack->setCurrentWidget(content);
}

void ReferenceCardView::editCommitted()
{
	// editingFinished fires again when leaveEditMode moves focus; the guard
	// keeps it to one pathCommitted per edit.
	if (committing || stack->currentWidget() != pathEdit)
		return;
	committing = true;
	const QString text = pathEdit->text();
	leaveEditMode();
	emit pathCommitted(text);
	committing = false;
}

bool ReferenceCardView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == pathEdit && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Escape)
		{
			// Abandon the edit: restore the last committed text so the
			// focus-out editingFinished cannot commit the abandoned draft.
			pathEdit->setText(currentState.editText);
			leaveEditMode();
			return true;
		}
	}
	if (watched == nameActivationWidget && event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (mouseEvent->button() == Qt::LeftButton
			&& currentState.nameClickable && !currentState.missing
			&& nameActivationWidget->rect().contains(mouseEvent->pos()))
		{
			emit nameActivated();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}
