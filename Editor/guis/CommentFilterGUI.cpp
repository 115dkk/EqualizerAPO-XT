/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "Editor/helpers/GUIHelper.h"
#include "CommentFilterGUI.h"
#include "ui_CommentFilterGUI.h"

using std::vector;
using std::wstring;

CommentFilterGUI::CommentFilterGUI(IFilterGUI* child, bool isComment)
	: ui(std::make_unique<Ui::CommentFilterGUI>()), child(child)
{
	ui->setupUi(this);

	connect(child, SIGNAL(updateModel()), this, SIGNAL(updateModel()));
	connect(child, SIGNAL(updateChannels()), this, SIGNAL(updateChannels()));

	ui->horizontalLayout->removeItem(ui->horizontalSpacer);
	ui->horizontalLayout->addWidget(child);

	ui->actionPowerOn->setChecked(!isComment);
	ui->toolBar->setIconSize(GUIHelper::scale(QSize(24, 24)));
	ui->toolBar->addAction(ui->actionPowerOn);
	ui->toolBar->updateMaximumHeight();

	child->setEnabled(!isComment);
}

CommentFilterGUI::~CommentFilterGUI() = default;

void CommentFilterGUI::configureChannels(vector<wstring>& channelNames)
{
	if (ui->actionPowerOn->isChecked())
	{
		child->configureChannels(channelNames);
	}
	else
	{
		// prevent modification of channel names
		vector<wstring> copy = channelNames;
		child->configureChannels(copy);
	}
}

void CommentFilterGUI::configureSelectedChannels(vector<wstring>& selectedChannels)
{
	// Every legacy row sits behind this decorator, so a missing forward here
	// left the whole LegacyRows presentation without the selection flow: a
	// VST row's channel fill offered nothing but silence, and a Channel row
	// could not narrow the rows below it. Same rule as configureChannels: a
	// powered-off line still sees the selection (its controls stay
	// meaningful) but cannot change what flows on, because the engine skips
	// commented lines.
	if (ui->actionPowerOn->isChecked())
	{
		child->configureSelectedChannels(selectedChannels);
	}
	else
	{
		vector<wstring> copy = selectedChannels;
		child->configureSelectedChannels(copy);
	}
}

void CommentFilterGUI::store(QString& command, QString& parameters)
{
	child->store(command, parameters);

	if (!ui->actionPowerOn->isChecked())
		command = "# " + command;
}

void CommentFilterGUI::loadPreferences(const QVariantMap& prefs)
{
	child->loadPreferences(prefs);
}

void CommentFilterGUI::storePreferences(QVariantMap& prefs)
{
	child->storePreferences(prefs);
}

void CommentFilterGUI::on_actionPowerOn_toggled(bool checked)
{
	child->setEnabled(checked);

	emit updateModel();
}
