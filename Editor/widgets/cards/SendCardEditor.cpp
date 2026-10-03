/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SendCardEditor.h"

#include <algorithm>
#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QVBoxLayout>

#include "audio/ChannelLayout.h"
#include "devices/AbstractAPOInfo.h"
#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

namespace
{
QLabel* makeCaption(const QString& text, QWidget* parent)
{
	QLabel* caption = new QLabel(text, parent);
	caption->setObjectName(QStringLiteral("SendFieldCaption"));
	return caption;
}

void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

void appendOnce(std::vector<std::wstring>& list, const std::wstring& name)
{
	if (std::find(list.begin(), list.end(), name) == list.end())
		list.push_back(name);
}
}

SendCardEditor::SendCardEditor(FilterTable* filterTable, const QString& parameters, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable)
{
	setObjectName(QStringLiteral("SendCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	model.load(parameters);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 2);
	layout->setSpacing(6);

	// Two rows: where and how in the first, timing in the second. One row
	// overflowed every skin's card width with an endpoint name of ordinary
	// length.
	QGridLayout* fields = new QGridLayout();
	fields->setContentsMargins(0, 0, 0, 0);
	fields->setHorizontalSpacing(8);
	fields->setVerticalSpacing(6);
	fields->setColumnStretch(1, 1);

	fields->addWidget(makeCaption(tr("To"), this), 0, 0);
	targetCombo = new QComboBox(this);
	targetCombo->setObjectName(QStringLiteral("SendTargetCombo"));
	targetCombo->setToolTip(tr("The playback endpoint that receives these channels"));
	// Fill the row rather than grow with the longest endpoint name.
	targetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	targetCombo->setMinimumContentsLength(16);
	targetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(targetCombo, SIGNAL(activated(int)), this, SLOT(targetChosen(int)));
	fields->addWidget(targetCombo, 0, 1);

	modeCombo = new QComboBox(this);
	modeCombo->setObjectName(QStringLiteral("SendModeCombo"));
	modeCombo->addItem(tr("Mix with its audio"), int(SendCommand::Mode::Mix));
	modeCombo->addItem(tr("Replace its audio"), int(SendCommand::Mode::Replace));
	modeCombo->setToolTip(tr("Whether the received channels are added to what the endpoint already plays, or take its place"));
	connect(modeCombo, SIGNAL(activated(int)), this, SLOT(modeChosen(int)));
	fields->addWidget(modeCombo, 0, 2);

	fields->addWidget(makeCaption(tr("Latency"), this), 1, 0);
	latencyEdit = new QLineEdit(this);
	latencyEdit->setObjectName(QStringLiteral("SendLatencyEdit"));
	latencyEdit->setPlaceholderText(tr("Default"));
	latencyEdit->setToolTip(tr("Milliseconds, or a whole number followed by \"samples\". Empty uses two processing periods of this device; raise it if the log reports underruns."));
	// Room for "960samples" in the widest skin font.
	latencyEdit->setMinimumWidth(104);
	latencyEdit->setMaximumWidth(140);
	connect(latencyEdit, SIGNAL(editingFinished()), this, SLOT(latencyEdited()));
	connect(latencyEdit, SIGNAL(textEdited(QString)), this, SLOT(latencyTyped()));
	fields->addWidget(latencyEdit, 1, 1, Qt::AlignLeft);

	compensateCheck = new QCheckBox(tr("Delay this device to match"), this);
	compensateCheck->setObjectName(QStringLiteral("SendCompensateCheck"));
	compensateCheck->setToolTip(tr("Delays this device's own channels by the same latency, so both devices play in time"));
	connect(compensateCheck, SIGNAL(toggled(bool)), this, SLOT(compensateToggled(bool)));
	fields->addWidget(compensateCheck, 1, 2);
	layout->addLayout(fields);

	// The validation line Hilbert, Velvet and NewChannel use, which every
	// skin dresses.
	warning = new QLabel(this);
	warning->setObjectName(QStringLiteral("CardValidationMessage"));
	warning->setWordWrap(true);
	warning->hide();
	layout->addWidget(warning);

	routingLayout = new QVBoxLayout();
	routingLayout->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(routingLayout);

	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("send");
	rowInfo.command = QStringLiteral("send");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updating = true;
	modeCombo->setCurrentIndex(model.mode() == SendCommand::Mode::Replace ? 1 : 0);
	latencyEdit->setText(model.latencyText());
	compensateCheck->setChecked(model.compensate());
	updating = false;

	loadEndpoints();
	rebuildRoutingView();
	updateWarning();
}

void SendCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Send");
	parameters = model.serialize();
}

void SendCardEditor::setChannelFlow(const ChannelFlowAtLine& flow)
{
	rowSources = flow.namesInScope;
	const size_t count = std::min(flow.deviceChannelCount, flow.namesInScope.size());
	ownChannels.assign(flow.namesInScope.begin(), flow.namesInScope.begin() + std::ptrdiff_t(count));
	loadEndpoints();
	rebuildRoutingView();
	updateWarning();
}

void SendCardEditor::loadEndpoints()
{
	std::vector<SendCardModel::Endpoint> endpoints;
	QString ownGuid;
	if (filterTable != nullptr)
	{
		const std::shared_ptr<AbstractAPOInfo> selected = filterTable->getSelectedDevice();
		if (selected != nullptr)
			ownGuid = QString::fromStdWString(selected->getDeviceGuid());
		for (const std::shared_ptr<AbstractAPOInfo>& device : filterTable->getOutputDevices())
		{
			// ASIO and Voicemeeter entries are not endpoints of the audio
			// service; a Send line cannot reach them.
			if (device == nullptr || !device->getTransportLabel().empty())
				continue;
			SendCardModel::Endpoint endpoint;
			endpoint.guid = QString::fromStdWString(device->getDeviceGuid());
			endpoint.name = QStringLiteral("%1 (%2)").arg(QString::fromStdWString(device->getConnectionName()),
				QString::fromStdWString(device->getDeviceName()));
			endpoint.channels = ChannelLayout::getChannelNames(int(device->getChannelCount()),
				int(device->getChannelMask()));
			endpoint.receives = device->receivesFromEndpoints();
			endpoints.push_back(endpoint);
		}
	}
	model.setEndpoints(ownGuid, endpoints);

	updating = true;
	targetCombo->clear();
	for (const SendCardModel::Endpoint& endpoint : model.targets())
	{
		targetCombo->addItem(endpoint.name, endpoint.guid);
		if (!endpoint.channels.empty() && !endpoint.receives)
			targetCombo->setItemData(targetCombo->count() - 1,
				tr("The Send option of this endpoint is off in the Device Selector"), Qt::ToolTipRole);
	}
	targetCombo->setCurrentIndex(model.targetIndex());
	targetCombo->setPlaceholderText(tr("Choose an endpoint"));
	updating = false;
}

void SendCardEditor::targetChosen(int index)
{
	if (updating || index < 0)
		return;
	model.setTarget(targetCombo->itemData(index).toString());
	rebuildRoutingView();
	updateWarning();
	emit updateModel();
}

void SendCardEditor::modeChosen(int index)
{
	if (updating || index < 0)
		return;
	model.setMode(SendCommand::Mode(modeCombo->itemData(index).toInt()));
	emit updateModel();
}

void SendCardEditor::latencyEdited()
{
	if (updating)
		return;
	if (!model.setLatencyText(latencyEdit->text()))
	{
		latencyEdit->setProperty("invalid", true);
		repolish(latencyEdit);
		return;
	}
	updating = true;
	latencyEdit->setText(model.latencyText());
	updating = false;
	emit updateModel();
}

void SendCardEditor::latencyTyped()
{
	if (!latencyEdit->property("invalid").toBool())
		return;
	latencyEdit->setProperty("invalid", false);
	repolish(latencyEdit);
}

void SendCardEditor::compensateToggled(bool on)
{
	if (updating)
		return;
	model.setCompensate(on);
	emit updateModel();
}

void SendCardEditor::routingEdited()
{
	if (routingView == nullptr)
		return;
	model.setAssignments(routingView->assignments());
	updateWarning();
	emit updateModel();
}

void SendCardEditor::rebuildRoutingView()
{
	// The old view may be the signal sender that led here (routingEdited), so
	// it cannot be deleted synchronously.
	if (routingView != nullptr)
	{
		routingLayout->removeWidget(routingView);
		routingView->hide();
		routingView->deleteLater();
		routingView = nullptr;
	}

	IRoutingRenderer* renderer = SkinManager::instance()->routingRenderer();
	if (renderer == nullptr || model.target().isEmpty())
		return;

	// Targets: the receiving endpoint's channels, then any the line names
	// that the endpoint does not have (so they stay visible and removable).
	std::vector<std::wstring> targets = model.targetChannels();
	for (const Assignment& assignment : model.assignments())
		appendOnce(targets, assignment.targetChannel);

	// Sources: this line's channels, device and virtual, then any the line
	// names that are not in scope here (the engine rejects those; showing
	// them lets the user disconnect them).
	RoutingPortModel portModel;
	std::vector<std::wstring> sources = rowSources;
	for (const Assignment& assignment : model.assignments())
		for (const Assignment::Summand& summand : assignment.sourceSum)
			appendOnce(sources, summand.channel);
	for (const std::wstring& source : sources)
		portModel.fixedSources.append(QString::fromStdWString(source));
	// Virtual is judged against both devices' channels: this device's for
	// the sources, the receiver's for the targets.
	portModel.deviceChannels = ownChannels;
	for (const std::wstring& channel : model.targetChannels())
		appendOnce(portModel.deviceChannels, channel);

	routingView = renderer->create(model.assignments(), targets, portModel, this,
		SkinManager::instance()->tokens());
	routingLayout->addWidget(routingView);
	connect(routingView, SIGNAL(routingChanged()), this, SLOT(routingEdited()));
}

void SendCardEditor::updateWarning()
{
	const QString text = model.warning();
	warning->setText(text);
	warning->setVisible(!text.isEmpty());
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Send, [](FilterTable* filterTable, const QString&, const QString& parameters) -> IFilterGUI* {
	// SendCommand owns the grammar. Text it rejects falls through to the raw
	// line editor rather than opening a card the first click would rewrite;
	// the picker's empty "Send:" opens an empty card, as MultiConvolution's
	// template does.
	SendCardModel probe;
	if (!probe.load(parameters))
		return nullptr;
	return new SendCardEditor(filterTable, parameters);
})
