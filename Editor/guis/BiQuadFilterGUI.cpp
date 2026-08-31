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

#include <cmath>
#include <numbers>
#include <QStandardItemModel>

#include "Editor/guis/BiQuadWidthConversion.h"
#include "Editor/helpers/GUIHelper.h"
#include "BiQuadFilterGUI.h"
#include "ui_BiQuadFilterGUI.h"

static const double dialFreqSteps = 1000;
static const double dialFreqMin = 20;
static const double dialFreqMax = 20000;
static const double dialQSteps = 1000;
static const double dialQMin = 0.3333;
static const double dialQMax = 33.3333;

BiQuadFilterGUI::BiQuadFilterGUI(const BiQuadCommand& command)
	: ui(std::make_unique<Ui::BiQuadFilterGUI>())
{
	ui->setupUi(this);

	// 84x66: every skin paints the knob in a centred square sized by the 66px
	// height, so the width beyond ~66px is pure horizontal padding. The trim
	// lets the row fit the 960px viewport in the widest-font skins.
	ui->freqDial->setFixedSize(GUIHelper::scale(QSize(84, 66)));
	ui->gainDial->setFixedSize(GUIHelper::scale(QSize(84, 66)));
	ui->qDial->setFixedSize(GUIHelper::scale(QSize(84, 66)));
	// Shared identity for the skins: these are genuine parameter mode
	// selectors, not labels, and skins may key their styling off these
	// properties without touching the legacy .ui.
	ui->typeComboBox->setProperty("filterSelector", true);
	ui->freqComboBox->setProperty("paramSelector", true);
	ui->qComboBox->setProperty("paramSelector", true);
	// Gain boosts or cuts around 0 dB; frequency and Q are one-directional.
	// The flag reaches the skin through KnobState so bipolar knobs can read
	// differently from unipolar ones.
	ui->gainDial->setBipolar(true);
	// The dial spans the user-configured comfort range (default ±20 dB) in
	// 0.1 dB steps; the spin box still accepts the full command range and a
	// larger typed gain just pegs the dial at its end.
	const double knobRange = GUIHelper::knobGainRange();
	ui->gainDial->setRange(qRound(-knobRange * 10.0), qRound(knobRange * 10.0));

	ui->typeComboBox->addItem(tr("Peaking filter"), BiQuad::PEAKING);
	ui->typeComboBox->addItem(tr("Low-pass filter"), BiQuad::LOW_PASS);
	ui->typeComboBox->addItem(tr("High-pass filter"), BiQuad::HIGH_PASS);
	ui->typeComboBox->addItem(tr("Band-pass filter"), BiQuad::BAND_PASS);
	ui->typeComboBox->addItem(tr("Low-shelf filter"), BiQuad::LOW_SHELF);
	ui->typeComboBox->addItem(tr("High-shelf filter"), BiQuad::HIGH_SHELF);
	ui->typeComboBox->addItem(tr("Notch filter"), BiQuad::NOTCH);
	ui->typeComboBox->addItem(tr("All-pass filter"), BiQuad::ALL_PASS);

	BiQuad::Type type = command.type;
	int typeIndex = ui->typeComboBox->findData(type);
	if (typeIndex != -1)
		ui->typeComboBox->setCurrentIndex(typeIndex);

	if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
		ui->freqComboBox->setCurrentIndex(command.isCornerFreq ? 1 : 0);

	ui->freqSpinBox->setValue(command.freq);

	if (type == BiQuad::PEAKING)
		ui->qComboBox->setCurrentIndex(command.isBandwidthOrS ? 1 : 0);
	else if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS)
		ui->qComboBox->setCurrentIndex(command.bandwidthOrQOrS == std::numbers::sqrt2_v<double> / 2.0 ? 0 : 1);
	else if ((type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF))
		ui->qComboBox->setCurrentIndex(command.isBandwidthOrS ? (command.bandwidthOrQOrS == 0.9 || command.isCornerFreq ? 0 : 1) : (command.isCornerFreq ? 1 : 2));
	else if (type == BiQuad::NOTCH)
		ui->qComboBox->setCurrentIndex(command.bandwidthOrQOrS == 30.0 ? 0 : 1);
	// The all-pass restores the mode it was written in, the way peaking does.
	// Without this the width-mode selector stayed on "Q factor" whatever the
	// line said, and store() then wrote a Q back.
	else if (type == BiQuad::ALL_PASS)
		ui->qComboBox->setCurrentIndex(command.isBandwidthOrS ? 1 : 0);

	if ((type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF) && command.isBandwidthOrS && command.bandwidthOrQOrS != 0.9)
		ui->qSpinBox->setValue(command.bandwidthOrQOrS * 12.0);
	else
		ui->qSpinBox->setValue(command.bandwidthOrQOrS);

	ui->gainSpinBox->setValue(command.dbGain);
}

BiQuadFilterGUI::~BiQuadFilterGUI() = default;

// This is the sole BiQuad serializer; the engine only parses. It emits from the
// combo-box mode selectors (Fixed/Q/BW/Slope, centre/corner) rather than from a
// BiQuadCommand, because those modes are not recoverable from the normalized
// struct - see BiQuadCommand.h for why there is no BiQuadCommand::serialize().
void BiQuadFilterGUI::store(QString& command, QString& parameters)
{
	BiQuad::Type type = (BiQuad::Type)ui->typeComboBox->currentData().toInt();
	QChar freqMode = ui->freqComboBox->currentData().toChar();
	QChar mode = ui->qComboBox->currentData().toChar();

	command = "Filter";
	parameters = "ON ";
	switch (type)
	{
	case BiQuad::PEAKING:
		parameters += "PK";
		break;
	case BiQuad::LOW_PASS:
		if (mode == 'F')
			parameters += "LP";
		else
			parameters += "LPQ";
		break;
	case BiQuad::HIGH_PASS:
		if (mode == 'F')
			parameters += "HP";
		else
			parameters += "HPQ";
		break;
	case BiQuad::BAND_PASS:
		parameters += "BP";
		break;
	case BiQuad::LOW_SHELF:
		if (freqMode == 'E' && mode != 'F')
			parameters += "LSC";
		else
			parameters += "LS";
		break;
	case BiQuad::HIGH_SHELF:
		if (freqMode == 'E' && mode != 'F')
			parameters += "HSC";
		else
			parameters += "HS";
		break;
	case BiQuad::NOTCH:
		parameters += "NO";
		break;
	case BiQuad::ALL_PASS:
		parameters += "AP";
		break;
	}

	if ((type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF) && mode == 'S')
		parameters += QString(" %1 dB").arg(ui->qSpinBox->value());

	parameters += QString(" Fc %1 Hz").arg(ui->freqSpinBox->value());

	if (type == BiQuad::PEAKING || type == BiQuad::LOW_SHELF
		|| type == BiQuad::HIGH_SHELF)
		parameters += QString(" Gain %1 dB").arg(ui->gainSpinBox->value());

	if (mode == 'Q')
		parameters += QString(" Q %1").arg(ui->qSpinBox->value());
	else if (mode == 'B')
		parameters += QString(" BW Oct %1").arg(ui->qSpinBox->value());
}

void BiQuadFilterGUI::on_freqDial_valueChanged(int value)
{
	ui->freqSpinBox->setValue(pow(dialFreqMax / dialFreqMin, value / dialFreqSteps) * dialFreqMin);
}

void BiQuadFilterGUI::on_freqSpinBox_valueChanged(double value)
{
	bool previousValue = ui->freqDial->blockSignals(true);
	ui->freqDial->setValue(round(dialFreqSteps * log(value / dialFreqMin) / log(dialFreqMax / dialFreqMin)));
	ui->freqDial->blockSignals(previousValue);

	emit updateModel();
}

void BiQuadFilterGUI::on_qDial_valueChanged(int value)
{
	ui->qSpinBox->setValue(pow(dialQMax / dialQMin, value / dialQSteps) * dialQMin);
}

void BiQuadFilterGUI::on_qSpinBox_valueChanged(double value)
{
	bool previousValue = ui->qDial->blockSignals(true);
	ui->qDial->setValue(round(dialQSteps * log(value / dialQMin) / log(dialQMax / dialQMin)));
	ui->qDial->blockSignals(previousValue);

	emit updateModel();
}

void BiQuadFilterGUI::on_gainDial_valueChanged(int value)
{
	ui->gainSpinBox->setValue(value * 0.1);
}

void BiQuadFilterGUI::on_gainSpinBox_valueChanged(double value)
{
	bool previousValue = ui->gainDial->blockSignals(true);
	ui->gainDial->setValue(round(value / 0.1));
	ui->gainDial->blockSignals(previousValue);

	emit updateModel();
}

void BiQuadFilterGUI::on_typeComboBox_currentIndexChanged(int index)
{
	BiQuad::Type type = (BiQuad::Type)ui->typeComboBox->itemData(index).toInt();
	bool sameTypeCategory = getTypeCategory(previousType) == getTypeCategory(type);

	QChar freqMode = ui->freqComboBox->currentData().toChar();
	double freqValue = ui->freqSpinBox->value();
	ui->freqComboBox->clear();
	if (type == BiQuad::PEAKING || type == BiQuad::BAND_PASS || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF || type == BiQuad::NOTCH || type == BiQuad::ALL_PASS)
		ui->freqComboBox->addItem(tr("Center frequency"), 'E');
	if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
		ui->freqComboBox->addItem(tr("Corner frequency"), 'O');
	ui->freqComboBox->setEnabled(ui->freqComboBox->count() > 1);
	int freqIndex = ui->freqComboBox->findData(freqMode);
	if (freqIndex != -1 && sameTypeCategory)
	{
		ui->freqComboBox->setCurrentIndex(freqIndex);
		ui->freqSpinBox->setValue(freqValue);
	}
	else
	{
		freqMode = ui->freqComboBox->currentData().toChar();
	}

	QChar mode = ui->qComboBox->currentData().toChar();
	double qValue = ui->qSpinBox->value();
	ui->qComboBox->clear();
	if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS || type == BiQuad::NOTCH)
		ui->qComboBox->addItem(tr("Fixed Q"), 'F');
	else if ((type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF) && freqMode == 'E')
		ui->qComboBox->addItem(tr("Fixed S"), 'F');
	if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
		ui->qComboBox->addItem(tr("Slope"), 'S');
	if (type == BiQuad::PEAKING || type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::ALL_PASS || type == BiQuad::BAND_PASS || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF || type == BiQuad::NOTCH)
		ui->qComboBox->addItem(tr("Q factor"), 'Q');
	// The all-pass used to be left with "Q factor" as its only entry, which the
	// setEnabled below then locked, so a line written as "BW Oct 1" was read as
	// a Q, redisplayed as one and saved as one - a different filter, about a
	// factor of sqrt(2) less group delay at Fc. The engine had always accepted
	// the bandwidth; only this list refused it.
	if (BiQuadWidth::offersBandwidth(type))
		ui->qComboBox->addItem(tr("Bandwidth"), 'B');
	ui->qComboBox->setEnabled(ui->qComboBox->count() > 1);
	int qIndex = ui->qComboBox->findData(mode);
	if (qIndex != -1 && sameTypeCategory)
	{
		ui->qComboBox->setCurrentIndex(qIndex);
		ui->qSpinBox->setValue(qValue);
	}
	else if (type == BiQuad::PEAKING || type == BiQuad::ALL_PASS)
	{
		ui->qSpinBox->setValue(BiQuadWidth::defaultQ(type));
	}

	bool gainVisible = type == BiQuad::PEAKING || type == BiQuad::LOW_SHELF
		|| type == BiQuad::HIGH_SHELF;
	ui->gainDial->setVisible(gainVisible);
	ui->gainLabel->setVisible(gainVisible);
	ui->gainSpinBox->setVisible(gainVisible);

	previousType = type;

	emit updateModel();
}

void BiQuadFilterGUI::on_freqComboBox_currentIndexChanged(int index)
{
	BiQuad::Type type = (BiQuad::Type)ui->typeComboBox->currentData().toInt();
	QChar freqMode = ui->freqComboBox->itemData(index).toChar();
	if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
	{
		QChar mode = ui->qComboBox->currentData().toChar();
		double qValue = ui->qSpinBox->value();
		ui->qComboBox->clear();
		if (freqMode == 'E')
			ui->qComboBox->addItem(tr("Fixed S"), 'F');
		ui->qComboBox->addItem(tr("Slope"), 'S');
		ui->qComboBox->addItem(tr("Q factor"), 'Q');
		ui->qComboBox->setEnabled(ui->qComboBox->count() > 1);
		int qIndex = ui->qComboBox->findData(mode);
		if (qIndex != -1)
		{
			ui->qComboBox->setCurrentIndex(qIndex);
			ui->qSpinBox->setValue(qValue);
		}

		double dbGain = ui->gainSpinBox->value();
		double s = (ui->qSpinBox->value() / 12.0);
		if (mode == 'Q')
		{
			double q = ui->qSpinBox->value();
			double a = pow(10, dbGain / 40);
			s = 1.0 / ((1.0 / (q * q) - 2.0) / (a + 1.0 / a) + 1.0);
		}

		double centerFreqFactor = pow(10.0, abs(dbGain) / 80.0 / s);
		double freq = ui->freqSpinBox->value();
		if ((freqMode == 'E') == (type == BiQuad::LOW_SHELF))
			freq *= centerFreqFactor;
		else
			freq /= centerFreqFactor;
		ui->freqSpinBox->setValue(freq);
	}
}

void BiQuadFilterGUI::on_qComboBox_currentIndexChanged(int index)
{
	QChar mode = ui->qComboBox->currentData().toChar();
	if (mode == 'F' || mode == 'Q')
		ui->qSpinBox->setSuffix("");
	else if (mode == 'B')
		ui->qSpinBox->setSuffix(" Oct");
	else if (mode == 'S')
		ui->qSpinBox->setSuffix(" dB/Oct");

	ui->qDial->setEnabled(mode != 'F');
	ui->qSpinBox->setEnabled(mode != 'F');

	if (mode == 'F')
	{
		BiQuad::Type type = (BiQuad::Type)ui->typeComboBox->currentData().toInt();
		if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS)
			ui->qSpinBox->setValue(std::numbers::sqrt2_v<double> / 2.0);
		else if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
			ui->qSpinBox->setValue(0.9);
		else if (type == BiQuad::NOTCH)
			ui->qSpinBox->setValue(30);

		qIsBwOrS = type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF;
	}
	else if (qIsBwOrS && mode == 'Q')
	{
		BiQuad::Type type = (BiQuad::Type)ui->typeComboBox->currentData().toInt();
		double q;
		if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF) // from S
		{
			double s = ui->qSpinBox->value();
			if (s != 0.9)
				s /= 12.0;
			double a = pow(10, ui->gainSpinBox->value() / 40);
			q = 1.0 / sqrt((a + 1.0 / a) * (1.0 / s - 1.0) + 2.0);
		}
		else // from BW
		{
			q = BiQuadWidth::qFromBandwidth(ui->qSpinBox->value());
		}
		ui->qSpinBox->setValue(q);
		qIsBwOrS = false;
	}
	else if (!qIsBwOrS && mode == 'B')
	{
		ui->qSpinBox->setValue(BiQuadWidth::bandwidthFromQ(ui->qSpinBox->value()));
		qIsBwOrS = true;
	}
	else if (!qIsBwOrS && mode == 'S')
	{
		double q = ui->qSpinBox->value();
		double a = pow(10, ui->gainSpinBox->value() / 40);
		double s = 1.0 / ((1.0 / (q * q) - 2.0) / (a + 1.0 / a) + 1.0);
		ui->qSpinBox->setValue(s * 12.0);
		qIsBwOrS = true;
	}
	else if (mode == 'S')
	{
		ui->qSpinBox->setValue(12.0);
		qIsBwOrS = true;
	}

	emit updateModel();
}

int BiQuadFilterGUI::getTypeCategory(BiQuad::Type type)
{
	switch (type)
	{
	case BiQuad::PEAKING:
		return 0;
	case BiQuad::LOW_PASS:
	case BiQuad::HIGH_PASS:
	case BiQuad::BAND_PASS:
		return 1;
	case BiQuad::LOW_SHELF:
	case BiQuad::HIGH_SHELF:
		return 2;
	case BiQuad::NOTCH:
		return 3;
	case BiQuad::ALL_PASS:
		return 4;
	}

	return -1;
}
