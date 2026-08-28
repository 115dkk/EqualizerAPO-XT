/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "HilbertCardEditor.h"

#include <algorithm>
#include <functional>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/FilterTable.h"
#include "Editor/analysis/AnalysisViewController.h"
#include "Editor/widgets/FlowLayout.h"
#include "Editor/widgets/SegmentedControl.h"
#include "Editor/widgets/cards/ChannelSelectionModel.h"
#include "Editor/widgets/cards/FilterCardEditorRegistry.h"
#include "devices/AbstractAPOInfo.h"
#include "audio/ChannelLayout.h"
#include "filters/ChannelCommand.h"
#include "filters/HilbertFilter.h"

namespace
{
unsigned tableSampleRate(FilterTable* table)
{
	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	const unsigned value = device == nullptr ? 0 : device->getSampleRate();
	return value == 0 ? 48000 : value;
}

std::vector<std::wstring> tableDeviceChannels(FilterTable* table)
{
	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	if (device == nullptr)
		return {};
	return ChannelLayout::getChannelNames(device->getChannelCount(),
		device->getChannelMask());
}

std::vector<std::wstring> parseSelection(const QString& text)
{
	ChannelCommand command;
	ChannelCommand::parse(L"Channel", text.toStdWString(), command);
	return command.channels;
}

QString selectionText(const std::vector<std::wstring>& channels)
{
	QStringList result;
	for (const std::wstring& channel : channels)
		result.append(QString::fromStdWString(channel));
	return result.join(QLatin1Char(' '));
}

QWidget* readoutBlock(QWidget* parent, const QString& caption, QLabel*& value)
{
	QWidget* block = new QWidget(parent);
	block->setObjectName(QStringLiteral("HilbertReadoutBlock"));
	QVBoxLayout* layout = new QVBoxLayout(block);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	QLabel* label = new QLabel(caption, block);
	label->setObjectName(QStringLiteral("HilbertCaption"));
	label->setAlignment(Qt::AlignCenter);
	layout->addWidget(label);
	value = new QLabel(block);
	value->setObjectName(QStringLiteral("HilbertReadout"));
	value->setAlignment(Qt::AlignCenter);
	layout->addWidget(value);
	block->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	return block;
}
}

// Two instances provide the Shift and Align roles. It deliberately reuses the
// Channel card's model and chip object names, so aliases, device order and all
// five skins stay one vocabulary instead of a second channel picker.
class ChannelRoleSelector final : public QWidget
{
public:
	ChannelRoleSelector(const QString& caption, const QString& initial,
		bool allowAll, QWidget* parent)
		: QWidget(parent), text(initial), allowAll(allowAll)
	{
		setObjectName(QStringLiteral("HilbertChannelRole"));
		QVBoxLayout* root = new QVBoxLayout(this);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(6);
		QLabel* label = new QLabel(caption, this);
		label->setObjectName(QStringLiteral("HilbertCaption"));
		root->addWidget(label);
		container = new QWidget(this);
		container->setObjectName(QStringLiteral("HilbertChannelChips"));
		flow = new FlowLayout(container, 0, 6, 6);
		root->addWidget(container);
		model.load(text, deviceChannels);
		rebuild();
	}

	void configure(const std::vector<std::wstring>& channels)
	{
		deviceChannels = channels;
		model.load(text, deviceChannels);
		rebuild();
	}

	std::vector<std::wstring> selection() const
	{
		return parseSelection(model.serialize());
	}

	std::function<void()> onChanged;

private:
	void commit()
	{
		text = model.serialize();
		if (onChanged)
			onChanged();
	}

	void rebuild()
	{
		while (QLayoutItem* item = flow->takeAt(0))
		{
			delete item->widget();
			delete item;
		}
		const bool all = allowAll && model.allSelected();
		if (allowAll)
		{
			QToolButton* button = new QToolButton(container);
			button->setObjectName(QStringLiteral("ChannelChip"));
			button->setProperty("allChannels", true);
			button->setText(QStringLiteral("ALL"));
			button->setCheckable(true);
			button->setMinimumHeight(40);
			button->setChecked(all);
			connect(button, &QToolButton::toggled, this, [this](bool checked) {
				if (updating)
					return;
				model.setAllSelected(checked);
				for (int i = 0; i < flow->count(); ++i)
				{
					QWidget* widget = flow->itemAt(i)->widget();
					if (widget != nullptr
						&& !widget->property("allChannels").toBool())
						widget->setEnabled(!checked);
				}
				commit();
			});
			flow->addWidget(button);
		}
		for (const ChannelChip& chip : model.chips())
		{
			QToolButton* button = new QToolButton(container);
			button->setObjectName(QStringLiteral("ChannelChip"));
			button->setText(chip.name);
			button->setCheckable(true);
			button->setMinimumHeight(40);
			button->setChecked(chip.selected);
			button->setProperty("customChannel", !chip.fromDevice);
			button->setEnabled(!all);
			const QString name = chip.name;
			connect(button, &QToolButton::toggled, this, [this, name, button](bool) {
				if (updating)
					return;
				model.toggle(name);
				if (allowAll && model.serialize().trimmed().isEmpty())
				{
					model.toggle(name);
					const QSignalBlocker blocker(button);
					button->setChecked(true);
					return;
				}
				commit();
			});
			flow->addWidget(button);
		}
		QLineEdit* add = new QLineEdit(container);
		add->setObjectName(QStringLiteral("ChannelChipAdd"));
		add->setPlaceholderText(tr("Add channel"));
		add->setMaximumWidth(124);
		add->setMinimumHeight(40);
		add->setEnabled(!all);
		connect(add, &QLineEdit::returnPressed, this, [this, add]() {
			if (!model.addCustom(add->text()))
				return;
			text = model.serialize();
			rebuild();
			if (onChanged)
				onChanged();
		});
		flow->addWidget(add);
	}

	ChannelSelectionModel model;
	QString text;
	bool allowAll = false;
	bool updating = false;
	std::vector<std::wstring> deviceChannels;
	QWidget* container = nullptr;
	FlowLayout* flow = nullptr;
};

HilbertCardEditor::HilbertCardEditor(const HilbertCommand& command,
	unsigned sampleRate, const std::vector<std::wstring>& deviceChannels,
	const QString& validationError, QWidget* parent)
	: IFilterGUI(parent), current(command), sampleRate(sampleRate == 0 ? 48000 : sampleRate),
	  deviceChannels(deviceChannels)
{
	setObjectName(QStringLiteral("HilbertCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);
	setToolTip(tr("A fixed 1025-tap linear-phase Hilbert FIR. Shifted channels receive the selected ±90° transform; aligned channels receive only its 512-sample group delay."));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(10);

	validation = new QLabel(validationError, this);
	validation->setObjectName(QStringLiteral("CardValidationMessage"));
	validation->setWordWrap(true);
	validation->setVisible(!validationError.isEmpty());
	root->addWidget(validation);

	QHBoxLayout* modeRow = new QHBoxLayout();
	modeRow->setContentsMargins(0, 0, 0, 0);
	modeRow->setSpacing(18);
	QWidget* directionBlock = new QWidget(this);
	directionBlock->setObjectName(QStringLiteral("HilbertModeBlock"));
	QVBoxLayout* directionLayout = new QVBoxLayout(directionBlock);
	directionLayout->setContentsMargins(0, 0, 0, 0);
	directionLayout->setSpacing(6);
	QLabel* directionCaption = new QLabel(tr("Direction"), directionBlock);
	directionCaption->setObjectName(QStringLiteral("HilbertCaption"));
	directionCaption->setAlignment(Qt::AlignCenter);
	directionLayout->addWidget(directionCaption);
	direction = new SegmentedControl(directionBlock);
	direction->setObjectName(QStringLiteral("HilbertDirection"));
	direction->setLabels({QStringLiteral("−90°"), QStringLiteral("+90°")});
	direction->setMinimumHeight(40);
	direction->setCurrentIndex(current.directionDegrees < 0 ? 0 : 1);
	connect(direction, &SegmentedControl::currentIndexChanged, this, [this](int index) {
		current.directionDegrees = index == 0 ? -90 : 90;
		changed();
		refreshReadouts();
	});
	directionLayout->addWidget(direction);
	modeRow->addWidget(directionBlock);

	QWidget* graphBlock = new QWidget(this);
	graphBlock->setObjectName(QStringLiteral("HilbertModeBlock"));
	QVBoxLayout* graphLayout = new QVBoxLayout(graphBlock);
	graphLayout->setContentsMargins(0, 0, 0, 0);
	graphLayout->setSpacing(6);
	QLabel* graphCaption = new QLabel(tr("Graph"), graphBlock);
	graphCaption->setObjectName(QStringLiteral("HilbertCaption"));
	graphCaption->setAlignment(Qt::AlignCenter);
	graphLayout->addWidget(graphCaption);
	graph = new SegmentedControl(graphBlock);
	graph->setObjectName(QStringLiteral("HilbertGraph"));
	graph->setLabels({tr("Phase"), tr("Group delay")});
	graph->setMinimumHeight(40);
	connect(graph, &SegmentedControl::currentIndexChanged, this, [](int index) {
		AnalysisViewController::instance()->requestMetric(index == 0
			? AnalysisMetric::PhaseDegrees : AnalysisMetric::GroupDelayMs);
	});
	graphLayout->addWidget(graph);
	modeRow->addWidget(graphBlock);
	// The four readouts share the switch row: the channel selectors below make
	// the card tall already, and a separate footer row of fixed figures spent
	// another line on nothing (the same judgement that folded the all-pass
	// card's footer into its knob row).
	modeRow->addSpacing(6);
	modeRow->addWidget(readoutBlock(this, tr("Phase"), phaseValue));
	modeRow->addWidget(readoutBlock(this, tr("Latency"), latencyValue));
	QLabel* taps = nullptr;
	modeRow->addWidget(readoutBlock(this, tr("FIR"), taps));
	taps->setText(tr("%1 taps").arg(HilbertTapCount));
	QLabel* magnitude = nullptr;
	modeRow->addWidget(readoutBlock(this, tr("Passband"), magnitude));
	magnitude->setText(QStringLiteral("0.0 dB"));
	modeRow->addStretch(1);
	root->addLayout(modeRow);

	shiftedSelector = new ChannelRoleSelector(tr("Phase-shifted channels"),
		selectionText(current.shiftedChannels), true, this);
	alignedSelector = new ChannelRoleSelector(tr("Latency-aligned channels"),
		selectionText(current.alignedChannels), false, this);
	shiftedSelector->onChanged = [this] { changed(); };
	alignedSelector->onChanged = [this] { changed(); };
	for (const std::vector<std::wstring>* role :
		{&current.shiftedChannels, &current.alignedChannels})
		for (const std::wstring& channel : *role)
			if (channel != L"ALL"
				&& std::find(this->deviceChannels.begin(),
					this->deviceChannels.end(), channel)
					== this->deviceChannels.end())
				this->deviceChannels.push_back(channel);
	shiftedSelector->configure(deviceChannels);
	alignedSelector->configure(deviceChannels);
	root->addWidget(shiftedSelector);
	root->addWidget(alignedSelector);
	refreshReadouts();
}

void HilbertCardEditor::store(QString& command, QString& parameters)
{
	current.shiftedChannels = shiftedSelector->selection();
	current.alignedChannels = alignedSelector->selection();
	current.directionDegrees = direction->currentIndex() == 0 ? -90 : 90;
	command = QStringLiteral("Hilbert");
	parameters = QString::fromStdWString(current.serialize());
}

void HilbertCardEditor::configureChannels(std::vector<std::wstring>& channelNames)
{
	for (const std::wstring& channel : channelNames)
		if (std::find(deviceChannels.begin(), deviceChannels.end(), channel)
			== deviceChannels.end())
			deviceChannels.push_back(channel);
	shiftedSelector->configure(deviceChannels);
	alignedSelector->configure(deviceChannels);
}

void HilbertCardEditor::changed()
{
	HilbertCommand candidate = current;
	candidate.shiftedChannels = shiftedSelector->selection();
	candidate.alignedChannels = alignedSelector->selection();
	HilbertCommand parsed;
	std::wstring error;
	if (!HilbertCommand::parse(L"Hilbert", candidate.serialize(), parsed, &error))
	{
		validation->setText(QString::fromStdWString(error));
		validation->setVisible(true);
		return;
	}
	current = candidate;
	validation->setVisible(false);
	emit updateModel();
}

void HilbertCardEditor::refreshReadouts()
{
	phaseValue->setText(current.directionDegrees < 0
		? QStringLiteral("−90°") : QStringLiteral("+90°"));
	const double milliseconds = 1000.0 * HilbertLatencySamples / sampleRate;
	latencyValue->setText(tr("%1 ms · %2 samples")
		.arg(milliseconds, 0, 'f', 2).arg(HilbertLatencySamples));
}

REGISTER_FILTER_CARD_EDITOR(Hilbert,
	[](FilterTable* table, const QString& command, const QString& parameters) -> IFilterGUI* {
		HilbertCommand parsed;
		std::wstring error;
		const bool valid = HilbertCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error);
		if (command != QStringLiteral("Hilbert"))
			return nullptr;
		return new HilbertCardEditor(parsed, tableSampleRate(table),
			tableDeviceChannels(table),
			valid ? QString() : QString::fromStdWString(error));
	})
