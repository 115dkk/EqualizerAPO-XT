/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MainToolbarKit.h"

#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QSizePolicy>
#include <QToolBar>
#include <QWidget>

#include "SkinComboBox.h"

namespace
{
QWidget* makeSpacer()
{
	QWidget* spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setAttribute(Qt::WA_NoSystemBackground, true);
	return spacer;
}

void addStandardAction(QToolBar* toolBar, const QString& text, const QString& name)
{
	QAction* action = toolBar->addAction(text);
	action->setObjectName(name);
}
}

MainToolbarKit::Widgets MainToolbarKit::populate(
	QToolBar* toolBar, const Content& content, bool addStandardActions)
{
	toolBar->setObjectName(QStringLiteral("MainToolBar"));
	toolBar->setMovable(false);
	if (addStandardActions)
	{
		addStandardAction(toolBar, QStringLiteral("New"), QStringLiteral("actionNew"));
		addStandardAction(toolBar, QStringLiteral("Open"), QStringLiteral("actionOpen"));
		addStandardAction(toolBar, QStringLiteral("Save"), QStringLiteral("actionSave"));
		toolBar->addSeparator();
		addStandardAction(toolBar, QStringLiteral("Undo"), QStringLiteral("actionUndo"));
		addStandardAction(toolBar, QStringLiteral("Redo"), QStringLiteral("actionRedo"));
	}

	Widgets widgets;
	QWidget* spacer = makeSpacer();
	spacer->setFixedWidth(10);
	toolBar->addWidget(spacer);

	widgets.instantMode = new QCheckBox(content.instantMode);
	widgets.instantMode->setObjectName(QStringLiteral("InstantModeCheckBox"));
	widgets.instantMode->setChecked(true);
	widgets.instantMode->setToolTip(content.instantModeToolTip);
	toolBar->addWidget(widgets.instantMode);

	widgets.dirtyStatus = new QLabel(content.saved);
	widgets.dirtyStatus->setObjectName(QStringLiteral("DirtyStatusBadge"));
	widgets.dirtyStatus->setToolTip(content.savedToolTip);
	toolBar->addWidget(widgets.dirtyStatus);

	spacer = makeSpacer();
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	toolBar->addWidget(spacer);

	QLabel* deviceLabel = new QLabel(content.device);
	deviceLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	toolBar->addWidget(deviceLabel);

	widgets.device = new SkinComboBox;
	widgets.device->setObjectName(QStringLiteral("ToolBarComboBox"));
	if (!content.deviceValue.isEmpty())
		widgets.device->addItem(content.deviceValue);
	toolBar->addWidget(widgets.device);

	widgets.deviceFormat = new QLabel(content.formatText);
	widgets.deviceFormat->setObjectName(QStringLiteral("DeviceFormatBadge"));
	widgets.deviceFormat->setAttribute(Qt::WA_StyledBackground, true);
	widgets.deviceFormat->setProperty("severity", content.formatSeverity);
	widgets.deviceFormat->setToolTip(content.formatToolTip);
	widgets.deviceFormat->setVisible(content.formatVisible);
	toolBar->addWidget(widgets.deviceFormat);

	spacer = makeSpacer();
	spacer->setFixedWidth(10);
	toolBar->addWidget(spacer);

	QLabel* channelLabel = new QLabel(content.channels);
	channelLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	toolBar->addWidget(channelLabel);

	widgets.channels = new SkinComboBox;
	widgets.channels->setObjectName(QStringLiteral("ToolBarComboBox"));
	widgets.channels->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	if (!content.channelValue.isEmpty())
		widgets.channels->addItem(content.channelValue);
	toolBar->addWidget(widgets.channels);
	return widgets;
}

const QStringList& MainToolbarKit::visibilityIsDataObjectNames()
{
	static const QStringList names = {
		QStringLiteral("RackToolbarEarSpacer"),
		QStringLiteral("DeviceFormatBadge")
	};
	return names;
}
