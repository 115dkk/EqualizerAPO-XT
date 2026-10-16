/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The add-channel field of the routing views: a QLineEdit the view creates
	on first use, shows empty and focused next to its add control, and reads
	back once when editing finishes. Every renderer used to carry its own
	copy of these lines. What differs per view stays with the view: where
	the field sits (skin geometry), its object name (the skin sheet's
	selector) and what a committed name does (RoutingGridModel::addChannel
	on the assignments, or on Light Trace's port model).
*/

#pragma once

#include <QLineEdit>
#include <QObject>
#include <QRect>
#include <QString>

class RoutingChannelEditor
{
public:
	// objectName is the skin sheet's selector for the field; centred gives
	// the field centred text.
	RoutingChannelEditor(const QString& objectName, bool centred)
		: objectName(objectName), centred(centred)
	{
	}

	// Shows the field at geometry, empty and focused. The first call creates
	// it as a child of view and routes editingFinished to view's commit.
	template <typename View>
	void open(View* view, void (View::*commit)(), const QRect& geometry)
	{
		if (field == nullptr)
		{
			field = new QLineEdit(view);
			field->setObjectName(objectName);
			if (centred)
				field->setAlignment(Qt::AlignCenter);
			QObject::connect(field, &QLineEdit::editingFinished, view, commit);
		}
		field->setGeometry(geometry);
		field->setText(QString());
		field->show();
		field->setFocus();
	}

	// Hides an open field and hands back its text, trimmed. False when the
	// field is not open; the caller then has nothing to commit.
	bool take(QString& name)
	{
		if (field == nullptr || !field->isVisible())
			return false;
		name = field->text().trimmed();
		field->hide();
		return true;
	}

	bool isOpen() const
	{
		return field != nullptr && field->isVisible();
	}

	// The gallery's "addChannel" state types a sample name into the field.
	void setText(const QString& text)
	{
		if (field != nullptr)
			field->setText(text);
	}

private:
	const QString objectName;
	const bool centred;
	// Owned by the view through the Qt parent chain.
	QLineEdit* field = nullptr;
};
