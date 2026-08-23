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

#include "MinimalSubwooferRoutingCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QList>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace
{
const char fullTextProperty[] = "minimalBassFullText";

void repolishChild(QWidget* widget)
{
	if (widget == nullptr)
		return;

	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

void configureElidedLabel(QLabel* label)
{
	label->setTextFormat(Qt::PlainText);
	label->setMinimumWidth(0);
	label->setSizePolicy(
		QSizePolicy::Ignored, QSizePolicy::Preferred);
}
}

MinimalSubwooferRoutingCardView::MinimalSubwooferRoutingCardView(
	QWidget* parent)
	: SubwooferRoutingCardView(parent)
{
	setObjectName(
		QStringLiteral("MinimalSubwooferRoutingCardView"));
	setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Fixed);

	// Two 40 px action targets, their spacing, and the required body
	// margins must remain representable even at the narrowest width.
	setMinimumWidth(112);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(5);
	root->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout* statusLine = new QHBoxLayout();
	statusLine->setContentsMargins(0, 0, 0, 0);
	statusLine->setSpacing(12);

	validityLabel = new QLabel(this);
	validityLabel->setObjectName(
		QStringLiteral("MinimalBassValidity"));
	validityLabel->setAccessibleName(
		tr("Bass-management validity"));
	validityLabel->setTextFormat(Qt::PlainText);
	validityLabel->setTextInteractionFlags(
		Qt::TextSelectableByMouse);
	validityLabel->setSizePolicy(
		QSizePolicy::Minimum, QSizePolicy::Preferred);
	statusLine->addWidget(
		validityLabel, 0, Qt::AlignVCenter);

	statusLine->addStretch(1);

	profileLabel = new QLabel(this);
	profileLabel->setObjectName(
		QStringLiteral("MinimalBassProfile"));
	profileLabel->setAccessibleName(
		tr("Bass-management profile"));
	profileLabel->setAlignment(
		Qt::AlignRight | Qt::AlignVCenter);
	configureElidedLabel(profileLabel);
	statusLine->addWidget(
		profileLabel, 1, Qt::AlignVCenter);

	root->addLayout(statusLine);

	readoutGrid = new QGridLayout();
	readoutGrid->setContentsMargins(0, 0, 0, 0);
	readoutGrid->setHorizontalSpacing(16);
	readoutGrid->setVerticalSpacing(2);
	readoutGrid->setColumnStretch(1, 1);

	addReadoutRow(0, tr("LAYOUT"), layoutValue,
		tr("Speaker layout"),
		tr("Physical channel layout in this subwoofer-routing state"));
	addReadoutRow(1, tr("XOVER"), crossoverValue,
		tr("Representative crossover"),
		tr("Representative high-pass and low-pass crossover corner; "
			"the full editor lists the per-group sections"));
	addReadoutRow(2, tr("LFE GAIN"), lfeGainValue,
		tr("Source LFE gain"),
		tr("Whether source LFE is preserved and its applied gain"));
	addReadoutRow(3, tr("TRIM"), trimValue,
		tr("Headroom trim"),
		tr("Automatic or manual headroom trim"));

	root->addLayout(readoutGrid);

	diagnosticLabel = new QLabel(this);
	diagnosticLabel->setObjectName(
		QStringLiteral("MinimalBassDiagnostic"));
	diagnosticLabel->setAccessibleName(
		tr("Bass-management diagnostics"));
	diagnosticLabel->setTextFormat(Qt::PlainText);
	diagnosticLabel->setTextInteractionFlags(
		Qt::TextSelectableByMouse);
	diagnosticLabel->setAlignment(
		Qt::AlignLeft | Qt::AlignTop);
	diagnosticLabel->setWordWrap(true);
	diagnosticLabel->setMinimumWidth(0);
	diagnosticLabel->setSizePolicy(
		QSizePolicy::Ignored, QSizePolicy::Preferred);
	diagnosticLabel->setVisible(false);
	root->addWidget(diagnosticLabel);

	actionRow = new QWidget(this);
	actionRow->setObjectName(
		QStringLiteral("MinimalBassActionRow"));
	actionRow->setAccessibleName(
		tr("Bass-management actions"));
	actionRow->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Minimum);
	actionRow->setVisible(false);

	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(8);
	actionLayout->addStretch(1);

	root->addWidget(actionRow);

	// Qualified on purpose: seeding the initial presentation from the
	// constructor must not dispatch to a further-derived override.
	MinimalSubwooferRoutingCardView::applyState(state());
}

void MinimalSubwooferRoutingCardView::addReadoutRow(
	int row, const QString& caption, QLabel*& valueLabel,
	const QString& accessibleName, const QString& toolTip)
{
	QLabel* captionLabel = new QLabel(caption, this);
	captionLabel->setObjectName(
		QStringLiteral("MinimalBassCaption"));
	captionLabel->setAccessibleName(
		tr("%1 caption").arg(accessibleName));
	captionLabel->setTextFormat(Qt::PlainText);
	captionLabel->setAlignment(
		Qt::AlignLeft | Qt::AlignVCenter);
	captionLabel->setToolTip(toolTip);
	captionLabel->setSizePolicy(
		QSizePolicy::Minimum, QSizePolicy::Preferred);
	readoutGrid->addWidget(captionLabel, row, 0);

	valueLabel = new QLabel(this);
	valueLabel->setObjectName(
		QStringLiteral("MinimalBassValue"));
	valueLabel->setAccessibleName(accessibleName);
	valueLabel->setToolTip(toolTip);
	valueLabel->setAlignment(
		Qt::AlignLeft | Qt::AlignVCenter);
	configureElidedLabel(valueLabel);
	readoutGrid->addWidget(valueLabel, row, 1);
}

void MinimalSubwooferRoutingCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(actionRow);
	button->setObjectName(
		QStringLiteral("MinimalBassActionButton"));
	button->setProperty("minimalBassAction", true);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setMinimumSize(40, 40);

	QString actionName = button->accessibleName();
	if (actionName.isEmpty())
		actionName = button->toolTip();
	if (actionName.isEmpty())
	{
		actionName = button->text();
		actionName.remove(QLatin1Char('&'));
	}

	QString commandText;
	if (actionButtonCount == 0)
	{
		commandText = tr("OPEN EDITOR");
		if (actionName.isEmpty())
			actionName = tr("Open editor");
	}
	else if (actionButtonCount == 1)
	{
		commandText = tr("PRESET");
		if (actionName.isEmpty())
			actionName = tr("Preset");
	}
	else
	{
		if (actionName.isEmpty())
			actionName = tr("Bass-management action");
		commandText = actionName.toUpper();
	}

	if (button->accessibleName().isEmpty())
		button->setAccessibleName(actionName);
	if (button->toolTip().isEmpty())
		button->setToolTip(actionName);

	button->setText(
		QStringLiteral("[ ")
		+ commandText
		+ QStringLiteral(" ]"));

	if (QToolButton* toolButton =
		qobject_cast<QToolButton*>(button))
	{
		toolButton->setToolButtonStyle(
			Qt::ToolButtonTextOnly);
	}

	// Before the trailing stretch: commands pack left with the print.
	actionLayout->insertWidget(actionLayout->count() - 1,
		button, 0, Qt::AlignVCenter);
	++actionButtonCount;

	actionRow->setVisible(true);
	repolishChild(button);
	actionLayout->invalidate();
	actionRow->updateGeometry();
	updateGeometry();

	QTimer::singleShot(0, this, [this]()
		{
			updateActionPresentation();
		});
}

void MinimalSubwooferRoutingCardView::applyState(
	const SubwooferRoutingCardState& state)
{
	const bool hasError =
		!state.valid || !state.errorText.isEmpty();
	const bool hasWarning =
		!hasError && !state.warningText.isEmpty();

	QString validityText;
	QString validityToolTip;
	QString validitySeverity;

	if (hasError)
	{
		validityText = tr("!! INVALID");
		validitySeverity = QStringLiteral("error");
		validityToolTip = state.errorText.isEmpty()
			? tr("The subwoofer-routing state is invalid")
			: tr("Invalid subwoofer-routing state: %1")
				.arg(state.errorText);
	}
	else if (hasWarning)
	{
		validityText = tr("! VALID / WARNING");
		validitySeverity = QStringLiteral("warning");
		validityToolTip =
			tr("The subwoofer-routing state is valid but has a warning: %1")
				.arg(state.warningText);
	}
	else
	{
		validityText = tr("+ VALID");
		validitySeverity = QStringLiteral("valid");
		validityToolTip =
			tr("The subwoofer-routing state is valid");
	}

	validityLabel->setText(validityText);
	validityLabel->setToolTip(validityToolTip);
	validityLabel->setProperty(
		"severity", validitySeverity);
	repolishChild(validityLabel);

	QString profileText;
	QString profileToolTip;
	QString profileState = QStringLiteral("normal");

	if (state.linkedProfile)
	{
		const QString profileName = state.profileName.isEmpty()
			? tr("UNNAMED")
			: state.profileName;

		// The profile line is a data readout; when the file is missing the
		// diagnostic line already carries the cause, so this line only
		// changes ink (profileState), not words - status is never posted
		// twice.
		profileText =
			tr("PROFILE  %1  [LINKED]")
				.arg(profileName);
		if (state.profileMissing)
		{
			profileToolTip =
				tr("Linked profile \"%1\" is missing")
					.arg(profileName);
			profileState = QStringLiteral("missing");
		}
		else
		{
			profileToolTip =
				tr("Linked subwoofer-routing profile \"%1\"")
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
			tr("Embedded subwoofer-routing profile \"%1\"")
				.arg(state.profileName);
	}

	profileLabel->setProperty(
		"profileState", profileState);
	setElidedText(
		profileLabel,
		profileText,
		tr("%1\n%2").arg(profileToolTip, profileText));
	repolishChild(profileLabel);

	const QString layoutText = state.layoutLabel.isEmpty()
		? tr("UNKNOWN")
		: state.layoutLabel;
	setElidedText(layoutValue, layoutText);

	const QString crossoverText =
		state.highPassHz > 0.0 || state.lowPassHz > 0.0
			? crossoverSummary(state)
			: tr("NONE");
	setElidedText(crossoverValue, crossoverText);

	const auto formatDb = [this](double value)
	{
		if (!std::isfinite(value))
			return QStringLiteral("--");

		if (std::abs(value) < 0.05)
			value = 0.0;

		const QString figure =
			QString::number(value, 'f', 1);
		return value > 0.0
			? tr("+%1 dB").arg(figure)
			: tr("%1 dB").arg(figure);
	};

	QString lfeGainText;
	if (!state.sourceLfePreserved)
	{
		lfeGainText = tr("NOT PRESERVED");
	}
	else
	{
		lfeGainText =
			tr("PRESERVED / %1")
				.arg(formatDb(state.sourceLfeGainDb));
	}
	setElidedText(lfeGainValue, lfeGainText);

	const QString trimText = state.headroomAuto
		? tr("AUTO / %1").arg(
			formatDb(state.headroomTrimDb))
		: tr("MANUAL / %1").arg(
			formatDb(state.headroomTrimDb));
	setElidedText(trimValue, trimText);

	// One diagnostic line, highest severity first. The validity label
	// already names the state; this line carries the cause, so the two
	// never repeat each other (status contract, review round 2).
	QString diagnosticText;
	if (!state.errorText.isEmpty())
	{
		diagnosticText =
			tr("!! %1").arg(state.errorText);
		diagnosticLabel->setProperty(
			"severity", QStringLiteral("error"));
	}
	else if (!state.warningText.isEmpty())
	{
		diagnosticText =
			tr("! %1").arg(state.warningText);
		diagnosticLabel->setProperty(
			"severity", QStringLiteral("warning"));
	}
	else
	{
		diagnosticLabel->setProperty(
			"severity", QStringLiteral("none"));
	}
	diagnosticLabel->setText(diagnosticText);
	diagnosticLabel->setToolTip(diagnosticText);

	diagnosticLabel->setVisible(
		!diagnosticText.isEmpty());
	repolishChild(diagnosticLabel);

	diagnosticLabel->updateGeometry();
	refreshElisions();
	updateActionPresentation();
	updateGeometry();
}

void MinimalSubwooferRoutingCardView::changeEvent(
	QEvent* event)
{
	SubwooferRoutingCardView::changeEvent(event);

	if (event->type() == QEvent::FontChange
		|| event->type() == QEvent::ApplicationFontChange
		|| event->type() == QEvent::StyleChange)
	{
		QTimer::singleShot(0, this, [this]()
			{
				refreshElisions();
				updateActionPresentation();
				updateGeometry();
			});
	}
}

void MinimalSubwooferRoutingCardView::resizeEvent(
	QResizeEvent* event)
{
	SubwooferRoutingCardView::resizeEvent(event);

	refreshElisions();
	updateActionPresentation();

	QTimer::singleShot(0, this, [this]()
		{
			refreshElisions();
			updateActionPresentation();
		});
}

void MinimalSubwooferRoutingCardView::refreshElisions()
{
	// cppcheck-suppress constVariable // the loop mutates the labels through
	// these pointers; only the pointer slots themselves are const
	QLabel* const labels[] =
	{
		profileLabel,
		layoutValue,
		crossoverValue,
		lfeGainValue,
		trimValue
	};

	for (QLabel* label : labels)
	{
		if (label == nullptr)
			continue;

		const QString fullText =
			label->property(fullTextProperty).toString();
		if (!fullText.isNull())
			setElidedText(
				label, fullText, label->toolTip());
	}
}

void MinimalSubwooferRoutingCardView::setElidedText(
	QLabel* label, const QString& fullText,
	const QString& toolTip)
{
	if (label == nullptr)
		return;

	label->setProperty(fullTextProperty, fullText);
	label->setToolTip(
		toolTip.isEmpty() ? fullText : toolTip);

	if (fullText.isEmpty())
	{
		label->clear();
		return;
	}

	const int availableWidth =
		label->contentsRect().width();
	if (availableWidth <= 0)
	{
		label->clear();
		return;
	}

	const QFontMetrics metrics(label->font());
	if (metrics.horizontalAdvance(fullText)
		<= availableWidth)
	{
		label->setText(fullText);
		return;
	}

	// Do not leave an ellipsis-only remnant at the right edge. If one
	// useful character plus the elision marker cannot fit, drop this
	// secondary readout and retain the complete value in its tooltip.
	const int minimumUsefulWidth =
		metrics.horizontalAdvance(QStringLiteral("M..."));
	if (availableWidth < minimumUsefulWidth)
	{
		label->clear();
		return;
	}

	label->setText(metrics.elidedText(
		fullText, Qt::ElideRight, availableWidth));
}

void MinimalSubwooferRoutingCardView::updateActionPresentation()
{
	if (actionRow == nullptr || !actionRow->isVisible())
		return;

	QList<QAbstractButton*> buttons;
	for (int index = 0;
		index < actionLayout->count();
		++index)
	{
		QLayoutItem* item =
			actionLayout->itemAt(index);
		if (item == nullptr)
			continue;

		QAbstractButton* button =
			qobject_cast<QAbstractButton*>(
				item->widget());
		if (button != nullptr)
			buttons.append(button);
	}

	if (buttons.isEmpty())
		return;

	for (QAbstractButton* button : buttons)
	{
		button->ensurePolished();

		if (QToolButton* toolButton =
			qobject_cast<QToolButton*>(button))
		{
			toolButton->setToolButtonStyle(
				Qt::ToolButtonTextOnly);
			toolButton->updateGeometry();
		}
	}

	const int availableWidth =
		actionRow->contentsRect().width();
	if (availableWidth <= 0)
		return;

	const auto requiredWidth = [this, &buttons]()
	{
		int width = 0;
		for (QAbstractButton* button : buttons)
		{
			width += qMax(
				button->minimumWidth(),
				button->sizeHint().width());
		}

		if (buttons.size() > 1)
		{
			width += actionLayout->spacing()
				* (buttons.size() - 1);
		}
		return width;
	};

	// Preserve both 40 px targets at narrow widths. PRESET drops to its
	// supplied icon before OPEN EDITOR; text returns automatically as
	// soon as the action line has sufficient room.
	for (int index = buttons.size() - 1;
		index >= 0
			&& requiredWidth() > availableWidth;
		--index)
	{
		QToolButton* toolButton =
			qobject_cast<QToolButton*>(
				buttons.at(index));
		if (toolButton == nullptr
			|| toolButton->icon().isNull())
		{
			continue;
		}

		toolButton->setToolButtonStyle(
			Qt::ToolButtonIconOnly);
		toolButton->updateGeometry();
	}

	actionLayout->invalidate();
}
