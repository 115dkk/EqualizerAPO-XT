/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ChannelCardEditor.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

#include "filters/ChannelCommand.h"

ChannelCardEditor::ChannelCardEditor(const QString& parameters, QWidget* parent)
	: IFilterGUI(parent), parameters(parameters)
{
	setObjectName(QStringLiteral("ChannelCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	allChip = new QToolButton(this);
	allChip->setObjectName(QStringLiteral("ChannelChip"));
	allChip->setText(QStringLiteral("ALL"));
	allChip->setCheckable(true);
	allChip->setToolTip(tr("Select every channel"));
	// Stable QSS handle for the master chip, parallel to the Device card's
	// "allDevices" property; skins style ALL differently from a single seat.
	allChip->setProperty("allChannels", true);
	connect(allChip, SIGNAL(toggled(bool)), this, SLOT(allToggled(bool)));
	layout->addWidget(allChip);

	chipLayout = new QHBoxLayout();
	chipLayout->setContentsMargins(0, 0, 0, 0);
	chipLayout->setSpacing(6);
	layout->addLayout(chipLayout);

	customEdit = new QLineEdit(this);
	customEdit->setObjectName(QStringLiteral("ChannelChipAdd"));
	customEdit->setPlaceholderText(tr("Add channel"));
	customEdit->setToolTip(tr("Add a custom or virtual channel name (e.g. VSL)"));
	customEdit->setMaximumWidth(124);
	connect(customEdit, SIGNAL(returnPressed()), this, SLOT(customEntered()));
	layout->addWidget(customEdit);

	layout->addStretch(1);

	model.load(parameters, deviceChannels);
	reloadChips();
}

void ChannelCardEditor::store(QString& command, QString& storedParameters)
{
	command = QStringLiteral("Channel");
	storedParameters = model.serialize();
}

void ChannelCardEditor::setChannelFlow(const ChannelFlowAtLine& flow)
{
	// Re-seed the chips with the names in scope at this line (the device's
	// channels plus what Copy created above), keeping the current selection
	// (parameters tracks the latest edit). The selection this line hands the
	// rows below is computed from the stored line (computeChannelFlow).
	deviceChannels = flow.namesInScope;
	model.load(parameters, deviceChannels);
	reloadChips();
}

void ChannelCardEditor::allToggled(bool checked)
{
	if (updating)
		return;

	model.setAllSelected(checked);
	// ALL wins over individual selections when written, like the legacy
	// dialog; the chips stay live so the next pick narrows from ALL to that
	// seat (ChannelSelectionModel::toggle). Chips only re-sync here - a
	// rebuild inside a chip's own toggled signal would delete the emitting
	// button.
	syncChipStates();
	commitSelection();
}

void ChannelCardEditor::syncChipStates()
{
	updating = true;
	const bool all = model.allSelected();
	allChip->setChecked(all);
	const QList<ChannelChip>& chips = model.chips();
	for (int i = 0; i < chipLayout->count() && i < chips.size(); i++)
	{
		QAbstractButton* button = qobject_cast<QAbstractButton*>(chipLayout->itemAt(i)->widget());
		if (button != nullptr)
			// While ALL is written no single seat is: the chips read as the
			// line does.
			button->setChecked(chips[i].selected && !all);
	}
	updating = false;
}

void ChannelCardEditor::customEntered()
{
	if (!model.addCustom(customEdit->text()))
		return;

	customEdit->clear();
	// Safe to rebuild: the sender is the line edit, not one of the chips.
	reloadChips();
	commitSelection();
}

void ChannelCardEditor::reloadChips()
{
	updating = true;

	while (QLayoutItem* child = chipLayout->takeAt(0))
	{
		delete child->widget();
		delete child;
	}

	const bool all = model.allSelected();
	allChip->setChecked(all);

	for (const ChannelChip& chip : model.chips())
	{
		QToolButton* button = new QToolButton(this);
		button->setObjectName(QStringLiteral("ChannelChip"));
		button->setText(chip.name);
		button->setCheckable(true);
		// Custom/virtual channels are stylable separately (the channel badges
		// use a dashed outline for them; skins may do the same here).
		button->setProperty("customChannel", !chip.fromDevice);
		button->setChecked(chip.selected && !all);
		const QString name = chip.name;
		connect(button, &QToolButton::toggled, this, [this, name](bool) {
			if (updating)
				return;
			const bool narrowed = model.allSelected();
			model.toggle(name);
			// Narrowing from ALL changed more than this chip: ALL released
			// and the other seats cleared.
			if (narrowed)
				syncChipStates();
			commitSelection();
		});
		chipLayout->addWidget(button);
	}

	updating = false;
}

void ChannelCardEditor::commitSelection()
{
	parameters = model.serialize();
	emit updateModel();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Channel, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	return new ChannelCardEditor(parameters);
})
