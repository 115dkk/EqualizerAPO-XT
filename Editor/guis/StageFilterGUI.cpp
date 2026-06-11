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

#include "filters/StageCommand.h"
#include "StageFilterGUI.h"
#include "ui_StageFilterGUI.h"

StageFilterGUI::StageFilterGUI(const QString& parameters)
	: ui(new Ui::StageFilterGUI)
{
	ui->setupUi(this);

	// Parse through the shared codec so the GUI accepts exactly what the engine
	// accepts.
	StageCommand cmd;
	StageCommand::parse(L"Stage", parameters.toStdWString(), cmd);
	ui->preMixCheckBox->setChecked(cmd.contains(StageCommand::preMix));
	ui->postMixCheckBox->setChecked(cmd.contains(StageCommand::postMix));
	ui->captureCheckBox->setChecked(cmd.contains(StageCommand::capture));
}

StageFilterGUI::~StageFilterGUI()
{
	delete ui;
}

void StageFilterGUI::store(QString& command, QString& parameters)
{
	command = "Stage";

	StageCommand cmd;
	if (ui->preMixCheckBox->isChecked())
		cmd.stages.push_back(StageCommand::preMix);
	if (ui->postMixCheckBox->isChecked())
		cmd.stages.push_back(StageCommand::postMix);
	if (ui->captureCheckBox->isChecked())
		cmd.stages.push_back(StageCommand::capture);

	parameters = QString::fromStdWString(cmd.serialize());
}

void StageFilterGUI::on_preMixCheckBox_toggled(bool checked)
{
	emit updateModel();
}

void StageFilterGUI::on_postMixCheckBox_toggled(bool checked)
{
	emit updateModel();
}

void StageFilterGUI::on_captureCheckBox_toggled(bool checked)
{
	emit updateModel();
}
