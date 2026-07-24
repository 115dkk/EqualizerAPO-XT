/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "StageCardEditor.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

StageCardEditor::StageCardEditor(const QString& parameters, QWidget* parent)
	: IFilterGUI(parent)
{
	setObjectName(QStringLiteral("StageCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	model.load(parameters);

	// One chip per engine stage. The property carries a stable QSS handle so a
	// skin can give each stage its own look without parsing the label text.
	auto makeChip = [this](const QString& token, const QString& label,
	                       const QString& property, const QString& tip) {
		QToolButton* chip = new QToolButton(this);
		chip->setObjectName(QStringLiteral("StageChip"));
		chip->setText(label);
		chip->setCheckable(true);
		chip->setChecked(model.isSelected(token));
		chip->setProperty("stage", property);
		chip->setToolTip(tip);
		connect(chip, SIGNAL(toggled(bool)), this, SLOT(chipToggled()));
		chips.push_back({chip, token});
		return chip;
	};
	auto makeLaneCaption = [this](const QString& text) {
		QLabel* caption = new QLabel(text, this);
		caption->setObjectName(QStringLiteral("StageLaneCaption"));
		return caption;
	};

	// The three stages are not flat peers: pre-mix and post-mix are the two
	// taps of the playback pipeline, in signal order, while capture belongs
	// to the recording pipeline. Two captioned lanes state that hierarchy,
	// and the chain arrow keeps the playback taps in processing order.
	QGridLayout* grid = new QGridLayout(this);
	grid->setContentsMargins(4, 2, 4, 2);
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);

	grid->addWidget(makeLaneCaption(tr("Playback")), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
	QHBoxLayout* playbackLane = new QHBoxLayout();
	playbackLane->setContentsMargins(0, 0, 0, 0);
	playbackLane->setSpacing(6);
	playbackLane->addWidget(makeChip(QStringLiteral("pre-mix"), tr("Pre-mix"), QStringLiteral("pre"),
		tr("Apply in each program's stream, before Windows mixes them")));
	QLabel* chainArrow = new QLabel(QStringLiteral("→"), this);
	chainArrow->setObjectName(QStringLiteral("StageChainArrow"));
	chainArrow->setToolTip(tr("Signal order: pre-mix runs before the streams are mixed, post-mix after"));
	playbackLane->addWidget(chainArrow);
	playbackLane->addWidget(makeChip(QStringLiteral("post-mix"), tr("Post-mix"), QStringLiteral("post"),
		tr("Apply to the device's mixed output (the default stage)")));
	playbackLane->addStretch(1);
	grid->addLayout(playbackLane, 0, 1);

	grid->addWidget(makeLaneCaption(tr("Recording")), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
	QHBoxLayout* recordingLane = new QHBoxLayout();
	recordingLane->setContentsMargins(0, 0, 0, 0);
	recordingLane->setSpacing(6);
	recordingLane->addWidget(makeChip(QStringLiteral("capture"), tr("Capture"), QStringLiteral("capture"),
		tr("Apply to recording devices")));

	// Tokens outside the vocabulary select no stage but are kept as written;
	// an inert chip shows them so the user sees why the line may do nothing.
	if (!model.unknownTokens().isEmpty())
	{
		QToolButton* unknownChip = new QToolButton(this);
		unknownChip->setObjectName(QStringLiteral("StageChipUnknown"));
		unknownChip->setText(model.unknownTokens().join(QLatin1Char(' ')));
		unknownChip->setEnabled(false);
		unknownChip->setToolTip(tr("Selectors the engine does not recognize; they are kept as written"));
		recordingLane->addWidget(unknownChip);
	}
	recordingLane->addStretch(1);
	grid->addLayout(recordingLane, 1, 1);

	grid->setColumnStretch(1, 1);
}

void StageCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Stage");
	parameters = model.serialize();
}

void StageCardEditor::chipToggled()
{
	for (const StageChip& chip : chips)
		model.setSelected(chip.token, chip.button->isChecked());
	emit updateModel();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Stage, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	return new StageCardEditor(parameters);
})
