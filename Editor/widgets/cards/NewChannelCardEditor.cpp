/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "NewChannelCardEditor.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
// U+00D7 MULTIPLICATION SIGN, built from its code point so the source stays
// pure ASCII (no /utf-8 flag is set for MSVC).
const QChar removeMark(0x00D7);

void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}
}

NewChannelCardEditor::NewChannelCardEditor(const QString& parameters, QWidget* parent)
	: IFilterGUI(parent)
{
	setObjectName(QStringLiteral("NewChannelCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* column = new QVBoxLayout(this);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(4);

	QHBoxLayout* row = new QHBoxLayout();
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(6);
	column->addLayout(row);

	chipLayout = new QHBoxLayout();
	chipLayout->setContentsMargins(0, 0, 0, 0);
	chipLayout->setSpacing(6);
	row->addLayout(chipLayout);

	addEdit = new QLineEdit(this);
	addEdit->setObjectName(QStringLiteral("NewChannelAdd"));
	addEdit->setPlaceholderText(tr("Add channel"));
	addEdit->setToolTip(tr("Name one or more new channels, separated by spaces or commas"));
	addEdit->setMaximumWidth(124);
	connect(addEdit, SIGNAL(returnPressed()), this, SLOT(addEntered()));
	connect(addEdit, SIGNAL(textEdited(QString)), this, SLOT(addEdited()));
	row->addWidget(addEdit);
	row->addStretch(1);

	warning = new QLabel(this);
	// The validation line Hilbert and Velvet use, which every skin dresses.
	warning->setObjectName(QStringLiteral("CardValidationMessage"));
	warning->setWordWrap(true);
	warning->hide();
	column->addWidget(warning);

	model.load(parameters);
	reloadChips();
	updateWarning();
}

void NewChannelCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("NewChannel");
	parameters = model.serialize();
}

void NewChannelCardEditor::setChannelFlow(const ChannelFlowAtLine& flow)
{
	// Only the device's own channels are off limits; the virtual names
	// declared above may be named again (the engine adds them to the
	// selection and nothing else).
	const size_t count = std::min(flow.deviceChannelCount, flow.namesInScope.size());
	model.setDeviceChannels(std::vector<std::wstring>(flow.namesInScope.begin(),
		flow.namesInScope.begin() + std::ptrdiff_t(count)));
	reloadChips();
	updateWarning();
}

void NewChannelCardEditor::addEntered()
{
	const NewChannelListModel::AddOutcome outcome = model.add(addEdit->text());
	if (!outcome.rejected.isEmpty())
	{
		// Keep the typed text so it can be corrected in place.
		addEdit->setProperty("invalid", true);
		addEdit->setToolTip(QStringLiteral("%1: %2").arg(outcome.rejected,
			NewChannelListModel::problemText(outcome.problem)));
		repolish(addEdit);
		return;
	}

	addEdit->clear();
	addEdited();
	if (outcome.added.isEmpty())
		return;

	reloadChips();
	updateWarning();
	commit();
}

void NewChannelCardEditor::addEdited()
{
	if (!addEdit->property("invalid").toBool())
		return;
	addEdit->setProperty("invalid", false);
	addEdit->setToolTip(tr("Name one or more new channels, separated by spaces or commas"));
	repolish(addEdit);
}

void NewChannelCardEditor::reloadChips()
{
	// deleteLater, not delete: a chip's own clicked signal leads here, and
	// deleting the emitting button inside it is undefined.
	while (QLayoutItem* child = chipLayout->takeAt(0))
	{
		if (QWidget* widget = child->widget())
		{
			widget->hide();
			widget->deleteLater();
		}
		delete child;
	}

	for (const QString& name : model.names())
	{
		QToolButton* chip = new QToolButton(this);
		chip->setObjectName(QStringLiteral("NewChannelChip"));
		chip->setText(name + QStringLiteral("  ") + removeMark);
		const NewChannelCommand::Problem problem = model.problem(name);
		// Stable QSS handle: skins draw a refused name in their warning
		// colour.
		chip->setProperty("invalid", problem != NewChannelCommand::Problem::None);
		chip->setToolTip(problem == NewChannelCommand::Problem::None
			? tr("Remove %1").arg(name)
			: tr("%1: %2. Click to remove it.").arg(name, NewChannelListModel::problemText(problem)));
		connect(chip, &QToolButton::clicked, this, [this, name]() {
			model.remove(name);
			reloadChips();
			updateWarning();
			commit();
		});
		chipLayout->addWidget(chip);
	}
}

void NewChannelCardEditor::updateWarning()
{
	QString text;
	if (model.names().isEmpty())
		text = tr("Name at least one channel. The engine skips an empty line.");
	else if (model.hasProblems())
		text = tr("The engine skips this whole line until the marked names are removed or renamed.");
	warning->setText(text);
	warning->setVisible(!text.isEmpty());
}

void NewChannelCardEditor::commit()
{
	emit updateModel();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(NewChannel, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	return new NewChannelCardEditor(parameters);
})
