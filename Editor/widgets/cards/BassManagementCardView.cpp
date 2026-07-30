#include "BassManagementCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"

BassManagementCardView::BassManagementCardView(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(tr("Bass management summary"));
	setToolTip(tr("Bass-management crossover, routing and headroom summary"));
}

void BassManagementCardView::setState(
	const BassManagementCardState& state)
{
	currentState = state;
	setEnabled(state.enabled);
	applyState(currentState);
}

const BassManagementCardState& BassManagementCardView::state() const
{
	return currentState;
}

void BassManagementCardView::mouseDoubleClickEvent(
	QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		emit openEditorRequested();
		event->accept();
		return;
	}

	QWidget::mouseDoubleClickEvent(event);
}

void BassManagementCardView::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Return
		|| event->key() == Qt::Key_Enter)
	{
		emit openEditorRequested();
		event->accept();
		return;
	}

	QWidget::keyPressEvent(event);
}

DefaultBassManagementCardView::DefaultBassManagementCardView(
	QWidget* parent)
	: BassManagementCardView(parent)
{
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	grid = new QGridLayout();
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(14);
	grid->setVerticalSpacing(5);

	addReadoutRow(0, tr("Layout"), layoutValue,
		tr("Speaker layout"),
		tr("Physical channel layout stored in this bass-management state"));
	addReadoutRow(1, tr("Paths"), pathsValue,
		tr("Speaker groups and bass paths"),
		tr("Number of speaker groups, bass paths and active matrix routes"));
	addReadoutRow(2, tr("Crossovers"), crossoverValue,
		tr("Representative crossovers"),
		tr("Representative high-pass and low-pass crossover sections"));
	addReadoutRow(3, tr("Source LFE"), sourceLfeValue,
		tr("Source LFE routing"),
		tr("Whether the source LFE signal is preserved and at what gain"));
	addReadoutRow(4, tr("Headroom"), headroomValue,
		tr("Headroom"),
		tr("Automatic or manual headroom trim"));
	addReadoutRow(5, tr("Profile"), profileValue,
		tr("Bass-management profile"),
		tr("Embedded state or linked profile name"));

	root->addLayout(grid);

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	statusLabel->setAccessibleName(tr("Bass-management status"));
	root->addWidget(statusLabel);

	QWidget* actionRow = new QWidget(this);
	actionRow->setAccessibleName(tr("Bass-management actions"));
	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	actionLayout->addStretch(1);
	root->addWidget(actionRow);
}

void DefaultBassManagementCardView::addReadoutRow(
	int row, const QString& caption, QLabel*& valueLabel,
	const QString& accessibleName, const QString& toolTip)
{
	QLabel* captionLabel = new QLabel(caption, this);
	captionLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	captionLabel->setToolTip(toolTip);
	grid->addWidget(captionLabel, row, 0);

	valueLabel = new QLabel(this);
	valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	valueLabel->setWordWrap(true);
	valueLabel->setAccessibleName(accessibleName);
	valueLabel->setToolTip(toolTip);

	QFont font(SkinManager::instance()->tokens().monoFontFamily);
	if (font.family().isEmpty())
		font.setStyleHint(QFont::Monospace);
	valueLabel->setFont(font);
	grid->addWidget(valueLabel, row, 1);
}

void DefaultBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	actionLayout->addWidget(button);
}

void DefaultBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	layoutValue->setText(state.layoutLabel.isEmpty()
		? tr("Unknown")
		: state.layoutLabel);

	pathsValue->setText(
		tr("%1 groups, %2 bass paths, %3 matrix routes")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount)
			.arg(state.activeMatrixEdges));

	QStringList crossovers;
	if (!state.representativeHighPass.isEmpty())
	{
		crossovers.append(
			tr("HP %1").arg(state.representativeHighPass));
	}
	if (!state.representativeLowPass.isEmpty())
	{
		crossovers.append(
			tr("LP %1").arg(state.representativeLowPass));
	}
	crossoverValue->setText(crossovers.isEmpty()
		? tr("None")
		: crossovers.join(QStringLiteral(" / ")));

	sourceLfeValue->setText(state.sourceLfePreserved
		? tr("Preserved at %1 dB").arg(
			QString::number(state.sourceLfeGainDb, 'f', 1))
		: tr("Not preserved"));

	if (state.headroomAuto)
	{
		headroomValue->setText(std::isfinite(state.headroomTrimDb)
			? tr("Auto, %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1))
			: tr("Auto, trim unavailable"));
	}
	else
	{
		headroomValue->setText(
			tr("Manual, %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1)));
	}

	QString profileText;
	if (state.linkedProfile)
	{
		profileText = state.profileName.isEmpty()
			? tr("Linked profile")
			: state.profileName;
		if (state.profileMissing)
			profileText += tr(" (missing)");
	}
	else
	{
		profileText = state.profileName.isEmpty()
			? tr("Embedded state")
			: state.profileName;
	}
	profileValue->setText(profileText);

	QStringList status;
	if (!state.errorText.isEmpty())
		status.append(tr("Error: %1").arg(state.errorText));
	if (!state.warningText.isEmpty())
		status.append(tr("Warning: %1").arg(state.warningText));
	statusLabel->setText(status.join(QLatin1Char('\n')));
	statusLabel->setVisible(!status.isEmpty());

	QPalette statusPalette = statusLabel->palette();
	if (!state.errorText.isEmpty())
	{
		statusPalette.setColor(QPalette::WindowText,
			statusPalette.color(QPalette::BrightText));
	}
	else
	{
		statusPalette.setColor(QPalette::WindowText,
			statusPalette.color(QPalette::Text));
	}
	statusLabel->setPalette(statusPalette);
}
