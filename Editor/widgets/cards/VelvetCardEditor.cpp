#include "VelvetCardEditor.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/FilterTable.h"
#include "Editor/widgets/FlowLayout.h"
#include "Editor/widgets/SegmentedControl.h"
#include "Editor/widgets/ValueScrubBox.h"
#include "Editor/widgets/cards/FilterCardEditorRegistry.h"
#include "Editor/widgets/cards/VelvetImpulsePreview.h"
#include "devices/AbstractAPOInfo.h"

namespace
{
unsigned tableSampleRate(FilterTable* table)
{
	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	const unsigned value = device == nullptr ? 0 : device->getSampleRate();
	return value == 0 ? 48000 : value;
}
}

VelvetCardEditor::VelvetCardEditor(const VelvetCommand& command,
	unsigned sampleRate, const QString& validationError, QWidget* parent)
	: IFilterGUI(parent), current(command), sampleRate(sampleRate == 0 ? 48000 : sampleRate)
{
	setObjectName(QStringLiteral("VelvetCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);
	setToolTip(tr("A sparse, unit-energy velvet-noise FIR. Each processed channel gets an independent kernel; Dynamic mode renews all kernels with an equal-power crossfade."));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(10);

	validation = new QLabel(validationError, this);
	validation->setObjectName(QStringLiteral("CardValidationMessage"));
	validation->setWordWrap(true);
	validation->setVisible(!validationError.isEmpty());
	root->addWidget(validation);

	QWidget* primaryWidget = new QWidget(this);
	primaryWidget->setObjectName(QStringLiteral("VelvetPrimaryRow"));
	FlowLayout* primary = new FlowLayout(primaryWidget, 0, 18, 10);
	QWidget* modeBlock = new QWidget(primaryWidget);
	modeBlock->setObjectName(QStringLiteral("VelvetValueBlock"));
	modeBlock->setMinimumWidth(150);
	QVBoxLayout* modeLayout = new QVBoxLayout(modeBlock);
	modeLayout->setContentsMargins(0, 0, 0, 0);
	modeLayout->setSpacing(6);
	QLabel* modeCaption = new QLabel(tr("Mode"), modeBlock);
	modeCaption->setObjectName(QStringLiteral("VelvetCaption"));
	modeCaption->setAlignment(Qt::AlignCenter);
	modeLayout->addWidget(modeCaption);
	mode = new SegmentedControl(modeBlock);
	mode->setObjectName(QStringLiteral("VelvetMode"));
	mode->setLabels({tr("Static"), tr("Dynamic")});
	mode->setMinimumHeight(40);
	mode->setCurrentIndex(current.parameters.dynamic ? 1 : 0);
	connect(mode, &SegmentedControl::currentIndexChanged, this, [this](int index) {
		current.parameters.dynamic = index == 1;
		applyModeVisibility();
		parametersChanged();
	});
	modeLayout->addWidget(mode);
	primary->addWidget(modeBlock);
	primary->addWidget(valueBlock(tr("Amount"), amount, 0.0, 100.0, 1.0, 1,
		QStringLiteral("%")));
	primary->addWidget(valueBlock(tr("Time spread"), length, 1.0, 100.0, 0.25, 2,
		QStringLiteral(" ms")));
	evolutionBlock = valueBlock(tr("Evolution"), evolution, 0.1, 60.0, 0.1, 2,
		QStringLiteral(" s"));
	primary->addWidget(evolutionBlock);
	root->addWidget(primaryWidget);

	amount->setValue(current.parameters.amount * 100.0);
	length->setValue(current.parameters.lengthMs);
	evolution->setValue(current.parameters.refreshSeconds);

	preview = new VelvetImpulsePreview(this);
	root->addWidget(preview);
	statistics = new QLabel(this);
	statistics->setObjectName(QStringLiteral("VelvetStatistics"));
	root->addWidget(statistics);

	advancedToggle = new QToolButton(this);
	advancedToggle->setObjectName(QStringLiteral("VelvetAdvancedToggle"));
	advancedToggle->setText(tr("Advanced"));
	advancedToggle->setCheckable(true);
	advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	connect(advancedToggle, &QToolButton::toggled,
		this, &VelvetCardEditor::setAdvanced);
	root->addWidget(advancedToggle, 0, Qt::AlignLeft);

	advancedPanel = new QWidget(this);
	advancedPanel->setObjectName(QStringLiteral("VelvetAdvancedPanel"));
	QGridLayout* advanced = new QGridLayout(advancedPanel);
	advanced->setContentsMargins(0, 0, 0, 0);
	advanced->setHorizontalSpacing(18);
	advanced->setVerticalSpacing(8);
	advanced->addWidget(valueBlock(tr("Density"), density, 100.0, 4000.0,
		10.0, 1, QStringLiteral(" /s")), 0, 0);
	transitionBlock = valueBlock(tr("Transition"), transition, 1.0, 2000.0,
		5.0, 1, QStringLiteral(" ms"));
	advanced->addWidget(transitionBlock, 0, 1);
	advanced->addWidget(valueBlock(tr("Decay"), decay, -120.0, 0.0,
		1.0, 1, QStringLiteral(" dB")), 0, 2);

	QWidget* variationBlock = valueBlock(tr("Variation"), variation, 1.0,
		4294967295.0, 1.0, 0, QString());
	QHBoxLayout* variationRow = new QHBoxLayout();
	variationRow->setContentsMargins(0, 0, 0, 0);
	variationRow->setSpacing(8);
	variationRow->addWidget(variationBlock);
	QToolButton* regenerate = new QToolButton(advancedPanel);
	regenerate->setObjectName(QStringLiteral("VelvetRegenerate"));
	regenerate->setText(tr("Regenerate"));
	regenerate->setMinimumHeight(40);
	regenerate->setToolTip(tr("Choose the next deterministic variation"));
	connect(regenerate, &QToolButton::clicked, this, [this] {
		std::uint64_t seed = static_cast<std::uint64_t>(
			std::llround(variation->value()));
		seed = (seed * 1664525ULL + 1013904223ULL) & 0xffffffffULL;
		if (seed == 0)
			seed = 1;
		variation->setValue(static_cast<double>(seed));
	});
	variationRow->addWidget(regenerate, 0, Qt::AlignBottom);
	advanced->addLayout(variationRow, 1, 0, 1, 3);
	root->addWidget(advancedPanel);

	density->setValue(current.parameters.density);
	transition->setValue(current.parameters.transitionMs);
	decay->setValue(current.parameters.decayDb);
	variation->setValue(static_cast<double>(current.parameters.seed));

	for (ValueScrubBox* box : {amount, length, evolution, density,
		transition, decay, variation})
		connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged),
			this, [this](double) { parametersChanged(); });

	setAdvanced(false);
	applyModeVisibility();
	refreshPreview();
}

QWidget* VelvetCardEditor::valueBlock(const QString& caption, ValueScrubBox*& box,
	double minimum, double maximum, double step, int decimals,
	const QString& suffix)
{
	QWidget* block = new QWidget(this);
	block->setObjectName(QStringLiteral("VelvetValueBlock"));
	QVBoxLayout* layout = new QVBoxLayout(block);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	QLabel* label = new QLabel(caption, block);
	label->setObjectName(QStringLiteral("VelvetCaption"));
	layout->addWidget(label);
	box = new ValueScrubBox(block);
	box->setObjectName(QStringLiteral("VelvetValue"));
	box->setRange(minimum, maximum);
	box->setSingleStep(step);
	box->setDecimals(decimals);
	box->setSuffix(suffix);
	box->setKeyboardTracking(false);
	box->setMinimumWidth(118);
	box->setMinimumHeight(40);
	layout->addWidget(box);
	return block;
}

void VelvetCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Velvet");
	parameters = QString::fromStdWString(current.serialize());
}

void VelvetCardEditor::configureChannels(std::vector<std::wstring>& channelNames)
{
	channelCount = static_cast<unsigned>(std::max<std::size_t>(channelNames.size(), 1));
	refreshPreview();
}

void VelvetCardEditor::applyModeVisibility()
{
	const bool dynamic = mode->currentIndex() == 1;
	evolutionBlock->setVisible(dynamic);
	transitionBlock->setVisible(dynamic);
}

void VelvetCardEditor::parametersChanged()
{
	current.parameters.dynamic = mode->currentIndex() == 1;
	current.parameters.amount = amount->value() / 100.0;
	current.parameters.lengthMs = length->value();
	current.parameters.refreshSeconds = evolution->value();
	current.parameters.density = density->value();
	const double maximumTransition = std::min(2000.0,
		current.parameters.refreshSeconds * 900.0);
	transition->setMaximum(maximumTransition);
	if (transition->value() > maximumTransition)
	{
		const QSignalBlocker blocker(transition);
		transition->setValue(maximumTransition);
	}
	current.parameters.transitionMs = transition->value();
	current.parameters.decayDb = decay->value();
	current.parameters.seed = static_cast<std::uint64_t>(
		std::llround(variation->value()));
	validation->setVisible(false);
	refreshPreview();
	emit updateModel();
}

void VelvetCardEditor::refreshPreview()
{
	velvet::Processor processor;
	if (!processor.prepare(sampleRate, std::max(2U, channelCount))
		|| !processor.setParameters(current.parameters))
		return;
	const velvet::Statistics values = processor.statistics();
	preview->setImpulse(processor.activeTaps(0),
		processor.activeTapCount(0), processor.tailSamples());
	statistics->setText(tr("%1 taps/channel · max zero-lag kernel correlation %2")
		.arg(values.tapsPerChannel)
		.arg(values.maximumZeroLagCorrelation, 0, 'f', 3));
}

void VelvetCardEditor::setAdvanced(bool expanded)
{
	advancedToggle->setChecked(expanded);
	advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	advancedPanel->setVisible(expanded);
	advancedPanel->setMinimumHeight(expanded
		? advancedPanel->sizeHint().height()
		: 0);
	advancedPanel->updateGeometry();
	if (layout() != nullptr)
	{
		layout()->invalidate();
		layout()->activate();
	}
	for (QWidget* ancestor = parentWidget(); ancestor != nullptr;
		ancestor = ancestor->parentWidget())
	{
		QScrollArea* scroll = qobject_cast<QScrollArea*>(ancestor);
		if (scroll == nullptr || scroll->widget() != this)
			continue;
		const int desired = qBound(24, layout()->sizeHint().height(), 600);
		scroll->setFixedHeight(desired);
		scroll->updateGeometry();
		break;
	}
	updateGeometry();
	QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
}

REGISTER_FILTER_CARD_EDITOR(Velvet,
	[](FilterTable* table, const QString& command, const QString& parameters) -> IFilterGUI* {
		VelvetCommand parsed;
		std::wstring error;
		const bool valid = VelvetCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error);
		if (command != QStringLiteral("Velvet"))
			return nullptr;
		return new VelvetCardEditor(parsed, tableSampleRate(table),
			valid ? QString() : QString::fromStdWString(error));
	})
