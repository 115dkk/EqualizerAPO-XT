/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VSTBusLayoutControls.h"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"

namespace
{
constexpr VST3BusLayout layouts[] = {
	VST3BusLayout::Auto,
	VST3BusLayout::Mono,
	VST3BusLayout::Stereo,
	VST3BusLayout::Surround40,
	VST3BusLayout::Surround41,
	VST3BusLayout::Surround50,
	VST3BusLayout::Surround51,
	VST3BusLayout::Surround61,
	VST3BusLayout::Surround71,
	VST3BusLayout::Surround712,
	VST3BusLayout::Surround714
};
}

VSTBusLayoutControls::VSTBusLayoutControls(QWidget* parent)
	: VSTBusLayoutControls(parent, true)
{
}

VSTBusLayoutControls::VSTBusLayoutControls(QWidget* parent, bool buildNeutralLayout)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("VSTBusLayoutControls"));
	setFrameShape(QFrame::NoFrame);
	setFocusPolicy(Qt::NoFocus);

	inputLabel = new QLabel(tr("&Input"), this);
	outputLabel = new QLabel(tr("&Output"), this);
	inputCombo = new QComboBox(this);
	outputCombo = new QComboBox(this);
	inputCombo->setObjectName(QStringLiteral("VSTBusInputLayout"));
	outputCombo->setObjectName(QStringLiteral("VSTBusOutputLayout"));
	inputCombo->setProperty("paramSelector", true);
	outputCombo->setProperty("paramSelector", true);
	inputCombo->setAccessibleName(tr("VST3 main input bus layout"));
	outputCombo->setAccessibleName(tr("VST3 main output bus layout"));
	inputLabel->setBuddy(inputCombo);
	outputLabel->setBuddy(outputCombo);

	for (VST3BusLayout layout : layouts)
	{
		const QString name = QString::fromWCharArray(vst3BusLayoutName(layout));
		inputCombo->addItem(name, static_cast<int>(layout));
		outputCombo->addItem(name, static_cast<int>(layout));
	}

	directionLabel = new QLabel(QString::fromUtf8("\u2192"), this);
	directionLabel->setObjectName(QStringLiteral("VSTBusDirection"));
	directionLabel->setAlignment(Qt::AlignCenter);
	directionLabel->setAccessibleName(tr("routes to"));

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(QStringLiteral("VSTBusStatus"));
	statusLabel->setTextFormat(Qt::PlainText);
	statusLabel->setWordWrap(true);
	statusLabel->setAccessibleName(tr("VST bus status"));

	removeButton = new QToolButton(this);
	removeButton->setObjectName(QStringLiteral("VSTBusRemoveLayouts"));
	removeButton->setText(tr("Remove ignored layouts"));
	removeButton->setToolTip(tr("Remove the Input and Output keys that VST2 ignores"));
	removeButton->setAccessibleName(tr("Remove ignored VST bus layouts"));
	removeButton->setAutoRaise(true);
	removeButton->setVisible(false);
	connect(inputCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(selectionChanged()));
	connect(outputCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(selectionChanged()));
	connect(removeButton, SIGNAL(clicked()), this, SIGNAL(removeLayoutsRequested()));
	setFocusProxy(inputCombo);
	setControlsEnabled(true);
	if (buildNeutralLayout)
		this->buildNeutralLayout();
	setStatus(QString(), StatusTone::Neutral);
}

void VSTBusLayoutControls::buildNeutralLayout()
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 2, 0, 0);
	root->setSpacing(5);

	QGridLayout* fields = new QGridLayout();
	fields->setContentsMargins(0, 0, 0, 0);
	fields->setHorizontalSpacing(8);
	fields->setVerticalSpacing(3);
	fields->setColumnStretch(0, 1);
	fields->setColumnStretch(2, 1);
	fields->addWidget(inputLabel, 0, 0);
	fields->addWidget(outputLabel, 0, 2);
	fields->addWidget(inputCombo, 1, 0);
	fields->addWidget(directionLabel, 1, 1);
	fields->addWidget(outputCombo, 1, 2);
	root->addLayout(fields);
	root->addWidget(statusLabel);
	root->addWidget(removeButton, 0, Qt::AlignLeft);
}

void VSTBusLayoutControls::setLayouts(VST3BusLayout input, VST3BusLayout output)
{
	const QSignalBlocker inputBlocker(inputCombo);
	const QSignalBlocker outputBlocker(outputCombo);
	selectLayout(inputCombo, input);
	selectLayout(outputCombo, output);
}

void VSTBusLayoutControls::setControlsEnabled(bool enabled, const QString& disabledReason)
{
	inputCombo->setEnabled(enabled);
	outputCombo->setEnabled(enabled);

	const QString inputTip = enabled
		? tr("Requested layout for the VST3 main input bus") : disabledReason;
	const QString outputTip = enabled
		? tr("Requested layout for the VST3 main output bus") : disabledReason;
	inputCombo->setToolTip(inputTip);
	inputLabel->setToolTip(inputTip);
	outputCombo->setToolTip(outputTip);
	outputLabel->setToolTip(outputTip);
	setToolTip(enabled ? QString() : disabledReason);
}

void VSTBusLayoutControls::setStatus(const QString& text, StatusTone tone, bool showRemoveAction)
{
	currentTone = tone;
	QPalette statusPalette = statusLabel->palette();
	statusPalette.setColor(QPalette::WindowText, statusColor());
	statusLabel->setPalette(statusPalette);
	statusLabel->setText(text);
	statusLabel->setVisible(!text.isEmpty());
	removeButton->setVisible(showRemoveAction);
	update();
}

QColor VSTBusLayoutControls::statusColor() const
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	switch (currentTone)
	{
	case StatusTone::Success: return QColor(tokens.success);
	case StatusTone::Warning: return QColor(tokens.warning);
	case StatusTone::Critical: return QColor(tokens.danger);
	case StatusTone::Neutral: return QColor(tokens.mutedText);
	}
	return QColor(tokens.mutedText);
}

void VSTBusLayoutControls::selectionChanged()
{
	emit layoutsEdited(selectedLayout(inputCombo), selectedLayout(outputCombo));
}

VST3BusLayout VSTBusLayoutControls::selectedLayout(const QComboBox* combo)
{
	return static_cast<VST3BusLayout>(combo->currentData().toInt());
}

void VSTBusLayoutControls::selectLayout(QComboBox* combo, VST3BusLayout layout)
{
	const int index = combo->findData(static_cast<int>(layout));
	combo->setCurrentIndex(index >= 0 ? index : 0);
}
