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

#include <QToolButton>

#include "Editor/widgets/FlowLayout.h"

StageCardEditor::StageCardEditor(const QString& parameters, QWidget* parent)
	: IFilterGUI(parent)
{
	setObjectName(QStringLiteral("StageCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	model.load(parameters);

	FlowLayout* flow = new FlowLayout(this, 0, 6, 6);

	// One chip per engine stage. The property carries a stable QSS handle so a
	// skin can give each stage its own look without parsing the label text.
	struct StageInfo
	{
		QString token;
		QString label;
		QString property;
		QString tip;
	};
	const StageInfo stages[] = {
		{ QStringLiteral("pre-mix"), tr("Pre-mix"), QStringLiteral("pre"),
		  tr("Apply in each program's stream, before Windows mixes them") },
		{ QStringLiteral("post-mix"), tr("Post-mix"), QStringLiteral("post"),
		  tr("Apply to the device's mixed output (the default stage)") },
		{ QStringLiteral("capture"), tr("Capture"), QStringLiteral("capture"),
		  tr("Apply to recording devices") },
	};

	for (const StageInfo& info : stages)
	{
		QToolButton* chip = new QToolButton(this);
		chip->setObjectName(QStringLiteral("StageChip"));
		chip->setText(info.label);
		chip->setCheckable(true);
		chip->setChecked(model.isSelected(info.token));
		chip->setProperty("stage", info.property);
		chip->setToolTip(info.tip);
		connect(chip, SIGNAL(toggled(bool)), this, SLOT(chipToggled()));
		flow->addWidget(chip);
		chips.push_back({chip, info.token});
	}

	// Tokens outside the vocabulary select no stage but are kept as written;
	// an inert chip shows them so the user sees why the line may do nothing.
	if (!model.unknownTokens().isEmpty())
	{
		QToolButton* unknownChip = new QToolButton(this);
		unknownChip->setObjectName(QStringLiteral("StageChipUnknown"));
		unknownChip->setText(model.unknownTokens().join(QLatin1Char(' ')));
		unknownChip->setEnabled(false);
		unknownChip->setToolTip(tr("Selectors the engine does not recognize; they are kept as written"));
		flow->addWidget(unknownChip);
	}
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
