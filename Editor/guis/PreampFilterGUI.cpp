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

#include <cmath>

#include "Editor/helpers/GUIHelper.h"
#include "PreampFilterGUI.h"
#include <filters/PreampCommand.h>
#include "ui_PreampFilterGUI.h"



PreampFilterGUI::PreampFilterGUI(double dbGain)
	: ui(std::make_unique<Ui::PreampFilterGUI>())
{
	ui->setupUi(this);

	ui->dial->setFixedSize(GUIHelper::scale(QSize(100, 66)));
	ui->doubleSpinBox->setValue(dbGain);
}

PreampFilterGUI::~PreampFilterGUI() = default;

void PreampFilterGUI::store(QString& command, QString& parameters)
{
	command = "Preamp";

	// Read the widget into the shared command struct, then serialize it back into
	// the canonical "<dB> dB" parameter string (PreampCommand::serialize uses %g:
	// C locale, six significant digits, trailing zeros stripped).
	PreampCommand cmd;
	cmd.dbGain = ui->doubleSpinBox->value();
	cmd.valid = true;
	cmd.noOp = std::abs(cmd.dbGain) < 1e-9;
	parameters = QString::fromStdWString(cmd.serialize());
}

void PreampFilterGUI::on_dial_valueChanged(int value)
{
	ui->doubleSpinBox->setValue(value * 0.1);
}

void PreampFilterGUI::on_doubleSpinBox_valueChanged(double value)
{
	bool previousValue = ui->dial->blockSignals(true);
	ui->dial->setValue(round(value / 0.1));
	ui->dial->blockSignals(previousValue);

	emit updateModel();
}
