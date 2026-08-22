/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#include "filters/ChannelCommand.h"
#include "ChannelFilterGUI.h"
#include "ChannelFilterGUIDialog.h"
#include "ui_ChannelFilterGUI.h"

using std::vector;
using std::wstring;

ChannelFilterGUI::ChannelFilterGUI(const QString& parameters, int selectedChannelMask)
	: ui(std::make_unique<Ui::ChannelFilterGUI>())
{
	ui->setupUi(this);

	this->selectedChannelMask = selectedChannelMask;

	scene = new ChannelFilterGUIScene;
	scene->setParent(this);
	ui->graphicsView->setScene(scene);
	ui->graphicsView->setBackgroundRole(QPalette::Window);
	connect(scene, SIGNAL(selectionChanged()), this, SLOT(updateSelectedChannels()));

	selectedChannels.clear();

	// Parse through the shared codec so the GUI accepts exactly what the engine
	// accepts (a plain split(' ') misses comma-separated selectors).
	ChannelCommand cmd;
	ChannelCommand::parse(L"Channel", parameters.toStdWString(), cmd);
	for (const wstring& channel : cmd.channels)
		selectedChannels.append(QString::fromStdWString(channel));
}

ChannelFilterGUI::~ChannelFilterGUI() = default;

void ChannelFilterGUI::configureChannels(vector<wstring>& channelNames)
{
	this->channelNames = channelNames;

	refreshGui();
}

void ChannelFilterGUI::configureSelectedChannels(std::vector<std::wstring>& flowSelection)
{
	// Same resolution as the card editor: this line's selector tokens over
	// the in-scope names configureChannels just delivered, so the legacy
	// rows mirror the engine's selection flow too.
	const QStringList current = scene != nullptr ? scene->getSelectedChannels() : selectedChannels;
	std::vector<std::wstring> tokens;
	for (const QString& channel : current)
		tokens.push_back(channel.toUpper().toStdWString());
	flowSelection = ChannelCommand::resolveSelection(tokens, channelNames);
}

void ChannelFilterGUI::store(QString& command, QString& parameters)
{
	command = "Channel";

	selectedChannels = scene->getSelectedChannels();

	ChannelCommand cmd;
	for (const QString& channel : selectedChannels)
		cmd.channels.push_back(channel.toStdWString());
	parameters = QString::fromStdWString(cmd.serialize());
}

void ChannelFilterGUI::on_pushButton_clicked()
{
	ChannelFilterGUIDialog dialog(this, selectedChannels, selectedChannelMask, channelNames);
	if (dialog.exec() == QDialog::Accepted)
	{
		selectedChannels = dialog.getSelectedChannels();
		refreshGui();
		emit updateModel();
	}
}

void ChannelFilterGUI::updateSelectedChannels()
{
	selectedChannels = scene->getSelectedChannels();
	updateModel();
}

void ChannelFilterGUI::refreshGui()
{
	scene->load(channelNames, selectedChannels);
}
