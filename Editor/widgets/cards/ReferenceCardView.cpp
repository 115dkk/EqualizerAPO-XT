/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ReferenceCardView.h"

#include <array>

#include <QAbstractButton>
#include <QBoxLayout>
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

QString actionRoleName(ReferenceCardView::ActionRole role)
{
	switch (role)
	{
	case ReferenceCardView::ActionRole::Browse: return QStringLiteral("browse");
	case ReferenceCardView::ActionRole::OpenTarget: return QStringLiteral("openTarget");
	case ReferenceCardView::ActionRole::Import: return QStringLiteral("import");
	case ReferenceCardView::ActionRole::OpenPanel: return QStringLiteral("openPanel");
	case ReferenceCardView::ActionRole::Options: return QStringLiteral("options");
	case ReferenceCardView::ActionRole::EditPath: return QStringLiteral("editPath");
	}
	return QString();
}
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

void ReferenceCardView::addActionButton(ActionRole role, QAbstractButton* button)
{
	if (button == nullptr)
		return;
	actionRegistry.insert(static_cast<int>(role), button);
	button->setProperty("refActionRole", actionRoleName(role));
	placeActionButton(role, button);
	updateSharedProperties();
}

QAbstractButton* ReferenceCardView::actionButton(ActionRole role) const
{
	return actionRegistry.value(static_cast<int>(role), nullptr);
}

void ReferenceCardView::placeBusStrip(QWidget* strip)
{
	// Neutral default: the strip joins the end of the content flow. Every
	// shipped skin overrides this with a seat inside its own geometry.
	strip->setParent(content);
	if (QBoxLayout* box = qobject_cast<QBoxLayout*>(content->layout()))
		box->addWidget(strip, 0, Qt::AlignVCenter);
}

bool ReferenceCardView::locateMode() const
{
	return referenceCardNeedsLocate(currentState);
}

void ReferenceCardView::setState(const ReferenceCardState& state)
{
	currentState = state;
	updateSharedProperties();
	applyState(currentState);
	repolish(this);

	if (nameActivationWidget != nullptr)
		nameActivationWidget->setCursor(state.nameClickable && !state.missing
			? Qt::PointingHandCursor : Qt::ArrowCursor);

	if (!pathEdit->hasFocus())
		pathEdit->setText(state.editText);
}

void ReferenceCardView::updateSharedProperties()
{
	const QString severity = referenceCardSeverityName(currentState.statusSeverity);
	const bool locate = locateMode();
	const std::array<QWidget*, 2> surfaces = { this, content };
	for (QWidget* surface : surfaces)
	{
		surface->setProperty("refKind", currentState.kind);
		surface->setProperty("refMissing", currentState.missing);
		surface->setProperty("refAbsolute", currentState.absolutePath);
		surface->setProperty("refSeverity", severity);
	}

	if (QAbstractButton* browse = actionButton(ActionRole::Browse))
	{
		browse->setProperty("refLocate", locate);
		browse->setProperty("locate", locate);
		repolish(browse);
	}
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
