/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CommandRowFrame.h"

#include <QLabel>
#include <QPainter>
#include <QStyle>

#include "Editor/SkinManager.h"

CommandRowFrame::CommandRowFrame(QWidget* parent)
	: QFrame(parent)
{
}

void CommandRowFrame::applyRowInfo(const CommandRowInfo& rowInfo, QWidget* header)
{
	info = rowInfo;
	const QList<QPair<const char*, QVariant>> properties = {
		{ "filterKind", info.command }, { "filterEnabled", info.enabled },
		{ "selected", info.selected }, { "focused", info.focused },
		{ "scopeDepth", info.depth }, { "logicDepth", info.logicDepth },
		{ "branchState", info.branchState }, { "lineSkipped", info.lineSkipped }
	};
	bool changed = false;
	bool propertiesChanged = false;
	for (const auto& property : properties)
	{
		if (this->property(property.first) == property.second)
			continue;
		setProperty(property.first, property.second);
		if (header != nullptr)
			header->setProperty(property.first, property.second);
		changed = true;
		propertiesChanged = true;
	}

	const QString frameStyle = SkinManager::instance()->cardFrameStyle(info);
	const QString headerStyle = SkinManager::instance()->cardHeaderStyle(info);
	if (styleSheet() != frameStyle)
	{
		setStyleSheet(frameStyle);
		changed = true;
	}
	if (header != nullptr && header->styleSheet() != headerStyle)
	{
		header->setStyleSheet(headerStyle);
		changed = true;
	}
	if (!changed)
		return;

	for (QWidget* widget : { static_cast<QWidget*>(this), header })
	{
		if (widget == nullptr)
			continue;
		widget->style()->unpolish(widget);
		widget->style()->polish(widget);
		widget->update();
	}
	if (propertiesChanged)
	{
		for (QLabel* label : findChildren<QLabel*>())
		{
			label->style()->unpolish(label);
			label->style()->polish(label);
			label->update();
		}
	}
}

const CommandRowInfo& CommandRowFrame::rowInfo() const
{
	return info;
}

void CommandRowFrame::paintEvent(QPaintEvent* event)
{
	// QFrame::paintEvent renders the stylesheet background/border; the skin
	// hook then draws on top (no-op in the neutral default).
	QFrame::paintEvent(event);

	// The hover flag is sampled at paint time instead of being pushed through
	// setRowInfo: hover repaints are triggered by the skin's own :hover QSS
	// rules (or the gallery's WA_UnderMouse equivalent), so the stored info
	// would always lag one state behind.
	CommandRowInfo paintInfo = info;
	paintInfo.hovered = underMouse();
	// Facts a body editor posts after construction ride a dynamic property
	// for the same reason hovered is sampled here: the stored info predates
	// them (the VST card posts the loaded ABI for rack's nameplate).
	paintInfo.formatTag = property("rowFormatTag").toString();

	QPainter painter(this);
	SkinManager::instance()->paintCardChrome(painter, rect(), paintInfo);
}
