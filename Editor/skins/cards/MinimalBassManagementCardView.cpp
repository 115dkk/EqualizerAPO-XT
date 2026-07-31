/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#include "MinimalBassManagementCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QTransform>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
void repolishChild(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

QWidget* createSeparator(QWidget* parent)
{
	QWidget* separator = new QWidget(parent);
	separator->setObjectName(QStringLiteral("MinimalBassSeparator"));
	separator->setFixedHeight(1);
	separator->setFocusPolicy(Qt::NoFocus);
	separator->setForegroundRole(QPalette::Mid);
	separator->setAttribute(Qt::WA_TransparentForMouseEvents);
	return separator;
}
}

MinimalBassManagementCardView::MinimalBassManagementCardView(
	QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(QStringLiteral("MinimalBassManagementCardView"));

	QFont monoFont(
		SkinManager::instance()->tokens().monoFontFamily);
	if (monoFont.family().isEmpty())
		monoFont.setStyleHint(QFont::Monospace);
	monoFont.setFixedPitch(true);
	setFont(monoFont);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(10, 8, 10, 8);
	root->setSpacing(6);

	QHBoxLayout* statusLine = new QHBoxLayout();
	statusLine->setContentsMargins(0, 0, 0, 0);
	statusLine->setSpacing(12);

	validityLabel = new QLabel(this);
	validityLabel->setObjectName(
		QStringLiteral("MinimalBassValidity"));
	validityLabel->setAccessibleName(
		tr("Bass-management validity"));
	validityLabel->setTextInteractionFlags(
		Qt::TextSelectableByMouse);
	validityLabel->setForegroundRole(QPalette::WindowText);

	QFont validityFont = monoFont;
	validityFont.setBold(true);
	validityLabel->setFont(validityFont);
	statusLine->addWidget(validityLabel, 0, Qt::AlignVCenter);

	statusLine->addStretch(1);

	profileLabel = new ElidedLabel(this);
	profileLabel->setObjectName(
		QStringLiteral("MinimalBassProfile"));
	profileLabel->setAccessibleName(
		tr("Bass-management profile"));
	profileLabel->setElideMode(Qt::ElideRight);
	profileLabel->setMinimumWidth(0);
	profileLabel->setSizePolicy(
		QSizePolicy::Preferred, QSizePolicy::Preferred);
	profileLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	profileLabel->setForegroundRole(QPalette::Mid);
	statusLine->addWidget(profileLabel, 0, Qt::AlignVCenter);

	root->addLayout(statusLine);

	readoutGrid = new QGridLayout();
	readoutGrid->setContentsMargins(0, 0, 0, 0);
	readoutGrid->setHorizontalSpacing(18);
	readoutGrid->setVerticalSpacing(3);
	readoutGrid->setColumnStretch(1, 1);

	addReadoutRow(0, tr("LAYOUT"), layoutValue,
		tr("Speaker layout"),
		tr("Physical channel layout in this bass-management state"));
	addReadoutRow(1, tr("HP"), highPassValue,
		tr("Representative high-pass crossover"),
		tr("Representative high-pass crossover section"));
	addReadoutRow(2, tr("LP"), lowPassValue,
		tr("Representative low-pass crossover"),
		tr("Representative low-pass crossover section"));
	addReadoutRow(3, tr("LFE GAIN"), lfeGainValue,
		tr("Source LFE gain"),
		tr("Whether source LFE is preserved and its applied gain"));
	addReadoutRow(4, tr("TRIM"), trimValue,
		tr("Headroom trim"),
		tr("Automatic or manual headroom trim"));

	root->addLayout(readoutGrid);

	readoutSeparator = createSeparator(this);
	root->addWidget(readoutSeparator);

	stageLabel = new ElidedLabel(this);
	stageLabel->setObjectName(QStringLiteral("MinimalBassStage"));
	stageLabel->setAccessibleName(
		tr("Bass-management signal stages"));
	stageLabel->setElideMode(Qt::ElideRight);
	stageLabel->setMinimumWidth(0);
	stageLabel->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Preferred);
	stageLabel->setForegroundRole(QPalette::WindowText);
	root->addWidget(stageLabel);

	diagnosticLabel = new QLabel(this);
	diagnosticLabel->setObjectName(
		QStringLiteral("MinimalBassDiagnostic"));
	diagnosticLabel->setAccessibleName(
		tr("Bass-management diagnostics"));
	diagnosticLabel->setTextInteractionFlags(
		Qt::TextSelectableByMouse);
	diagnosticLabel->setWordWrap(true);
	diagnosticLabel->setForegroundRole(QPalette::WindowText);
	diagnosticLabel->setVisible(false);
	root->addWidget(diagnosticLabel);

	actionSeparator = createSeparator(this);
	actionSeparator->setVisible(false);
	root->addWidget(actionSeparator);

	actionRow = new QWidget(this);
	actionRow->setObjectName(
		QStringLiteral("MinimalBassActionRow"));
	actionRow->setAccessibleName(
		tr("Bass-management actions"));
	actionRow->setVisible(false);

	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	actionLayout->addStretch(1);
	root->addWidget(actionRow);

	connect(SkinManager::instance(), &SkinManager::skinChanged,
		this, [this]()
		{
			update();
		});

	// Qualified on purpose: seeding the initial presentation from the
	// constructor must not dispatch to a further-derived override.
	MinimalBassManagementCardView::applyState(state());
}

void MinimalBassManagementCardView::addReadoutRow(
	int row, const QString& caption, ElidedLabel*& valueLabel,
	const QString& accessibleName, const QString& toolTip)
{
	QLabel* captionLabel = new QLabel(caption, this);
	captionLabel->setObjectName(
		QStringLiteral("MinimalBassCaption"));
	captionLabel->setAlignment(
		Qt::AlignLeft | Qt::AlignVCenter);
	captionLabel->setToolTip(toolTip);
	captionLabel->setForegroundRole(QPalette::Mid);
	readoutGrid->addWidget(captionLabel, row, 0);

	valueLabel = new ElidedLabel(this);
	valueLabel->setObjectName(
		QStringLiteral("MinimalBassValue"));
	valueLabel->setAccessibleName(accessibleName);
	valueLabel->setToolTip(toolTip);
	valueLabel->setElideMode(Qt::ElideRight);
	valueLabel->setMinimumWidth(0);
	valueLabel->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Preferred);
	valueLabel->setForegroundRole(QPalette::WindowText);
	readoutGrid->addWidget(valueLabel, row, 1);
}

void MinimalBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(actionRow);
	button->setProperty("minimalBassAction", true);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setMinimumWidth(40);
	button->setMinimumHeight(40);

	QString actionName = button->accessibleName();
	if (actionName.isEmpty())
		actionName = button->toolTip();
	if (actionName.isEmpty())
	{
		actionName = button->text();
		actionName.remove(QLatin1Char('&'));
	}
	if (actionName.isEmpty())
		actionName = tr("Bass-management action");

	if (button->accessibleName().isEmpty())
		button->setAccessibleName(actionName);
	if (button->toolTip().isEmpty())
		button->setToolTip(actionName);

	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
	actionSeparator->setVisible(true);
	actionRow->setVisible(true);
	repolishChild(button);
}

void MinimalBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool effectiveValid =
		state.valid && state.errorText.isEmpty();

	validityLabel->setText(effectiveValid
		? tr("+ VALID")
		: tr("! INVALID"));

	if (!effectiveValid)
	{
		validityLabel->setToolTip(state.errorText.isEmpty()
			? tr("The bass-management state is invalid")
			: tr("Invalid bass-management state: %1")
				.arg(state.errorText));
	}
	else if (!state.warningText.isEmpty())
	{
		validityLabel->setToolTip(
			tr("The bass-management state is valid but has a warning: %1")
				.arg(state.warningText));
	}
	else
	{
		validityLabel->setToolTip(
			tr("The bass-management state is valid"));
	}

	QString profileText;
	QString profileToolTip;
	if (state.linkedProfile)
	{
		const QString profileName = state.profileName.isEmpty()
			? tr("UNNAMED")
			: state.profileName;

		if (state.profileMissing)
		{
			profileText =
				tr("PROFILE  %1  [LINKED / MISSING]")
					.arg(profileName);
			profileToolTip =
				tr("Linked profile \"%1\" is missing")
					.arg(profileName);
		}
		else
		{
			profileText =
				tr("PROFILE  %1  [LINKED]")
					.arg(profileName);
			profileToolTip =
				tr("Linked bass-management profile \"%1\"")
					.arg(profileName);
		}
	}
	else if (state.profileName.isEmpty())
	{
		profileText = tr("PROFILE  EMBEDDED");
		profileToolTip =
			tr("Bass-management state is embedded");
	}
	else
	{
		profileText =
			tr("PROFILE  %1  [EMBEDDED]")
				.arg(state.profileName);
		profileToolTip =
			tr("Embedded bass-management profile \"%1\"")
				.arg(state.profileName);
	}
	profileLabel->setFullText(profileText);
	profileLabel->setToolTip(profileToolTip);

	layoutValue->setFullText(state.layoutLabel.isEmpty()
		? tr("UNKNOWN")
		: state.layoutLabel);

	highPassValue->setFullText(
		state.representativeHighPass.isEmpty()
			? tr("NONE")
			: state.representativeHighPass);

	lowPassValue->setFullText(
		state.representativeLowPass.isEmpty()
			? tr("NONE")
			: state.representativeLowPass);

	const auto formatDb = [this](double value)
	{
		if (!std::isfinite(value))
			return tr("UNAVAILABLE");

		if (std::abs(value) < 0.05)
			value = 0.0;

		const QString figure =
			QString::number(value, 'f', 1);
		return value > 0.0
			? tr("+%1 dB").arg(figure)
			: tr("%1 dB").arg(figure);
	};

	if (!state.sourceLfePreserved)
	{
		lfeGainValue->setFullText(tr("NOT PRESERVED"));
	}
	else if (!std::isfinite(state.sourceLfeGainDb))
	{
		lfeGainValue->setFullText(
			tr("PRESERVED / GAIN UNAVAILABLE"));
	}
	else
	{
		lfeGainValue->setFullText(
			tr("PRESERVED / %1")
				.arg(formatDb(state.sourceLfeGainDb)));
	}

	if (!std::isfinite(state.headroomTrimDb))
	{
		trimValue->setFullText(state.headroomAuto
			? tr("AUTO / UNAVAILABLE")
			: tr("MANUAL / UNAVAILABLE"));
	}
	else
	{
		trimValue->setFullText(state.headroomAuto
			? tr("AUTO / %1")
				.arg(formatDb(state.headroomTrimDb))
			: tr("MANUAL / %1")
				.arg(formatDb(state.headroomTrimDb)));
	}

	stageLabel->setFullText(
		tr("MAIN -> BASS -> OUT  |  %1 GROUPS  |  %2 BASS PATHS")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount));
	stageLabel->setToolTip(
		tr("MAIN to BASS to OUT; %1 speaker groups, %2 bass paths, "
			"%3 active matrix routes")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount)
			.arg(state.activeMatrixEdges));

	QStringList diagnostics;
	if (!state.errorText.isEmpty())
	{
		diagnostics.append(
			tr("! ERROR: %1").arg(state.errorText));
	}
	if (!state.warningText.isEmpty())
	{
		diagnostics.append(
			tr("! WARNING: %1").arg(state.warningText));
	}

	diagnosticLabel->setText(
		diagnostics.join(QLatin1Char('\n')));
	diagnosticLabel->setToolTip(
		diagnostics.join(QLatin1Char('\n')));
	diagnosticLabel->setVisible(!diagnostics.isEmpty());
}

void MinimalBassManagementCardView::paintEvent(
	QPaintEvent* event)
{
	BassManagementCardView::paintEvent(event);

	paintSeparator(readoutSeparator);
	paintSeparator(actionSeparator);
}

void MinimalBassManagementCardView::paintSeparator(
	QWidget* separator)
{
	if (separator == nullptr || !separator->isVisible())
		return;

	const QRect separatorRect = separator->geometry();
	const qreal logicalY =
		separatorRect.top() + separatorRect.height() / 2.0;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	const QTransform deviceTransform = painter.deviceTransform();
	bool invertible = false;
	const QTransform inverse =
		deviceTransform.inverted(&invertible);

	qreal alignedY = logicalY;
	if (invertible)
	{
		QPointF devicePoint =
			deviceTransform.map(QPointF(0.0, logicalY));
		devicePoint.setY(
			std::floor(devicePoint.y()) + 0.5);
		alignedY = inverse.map(devicePoint).y();
	}

	QPen pen(separator->palette().color(
		separator->foregroundRole()));
	pen.setCosmetic(true);
	pen.setWidth(0);
	painter.setPen(pen);
	painter.drawLine(
		QPointF(separatorRect.left(), alignedY),
		QPointF(separatorRect.left()
			+ separatorRect.width(), alignedY));
}
