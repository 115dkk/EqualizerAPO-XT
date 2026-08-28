/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DefaultReferenceCardView.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
QString iconResourceFor(const ReferenceCardState& state)
{
	if (state.missing)
		return QStringLiteral(":/icons/modern/alert-triangle.svg");
	if (state.kind == QStringLiteral("vst"))
		return QStringLiteral(":/icons/modern/plugin.svg");
	if (state.kind == QStringLiteral("include"))
		return QStringLiteral(":/icons/modern/file-include.svg");
	return QStringLiteral(":/icons/modern/waveform.svg");
}
}

DefaultReferenceCardView::DefaultReferenceCardView(QWidget* parent)
	: ReferenceCardView(parent)
{
	QWidget* page = contentWidget();
	rootLayout = new QHBoxLayout(page);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(10);

	iconLabel = new QLabel(page);
	iconLabel->setObjectName(QStringLiteral("RefIcon"));
	rootLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

	QWidget* textBlock = new QWidget(page);
	QVBoxLayout* textLayout = new QVBoxLayout(textBlock);
	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->setSpacing(2);

	QWidget* nameRow = new QWidget(textBlock);
	QHBoxLayout* nameLayout = new QHBoxLayout(nameRow);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	nameLayout->setSpacing(6);

	nameLabel = new QLabel(nameRow);
	nameLabel->setObjectName(QStringLiteral("RefName"));
	installNameActivation(nameLabel);
	nameLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

	formatBadge = new QLabel(nameRow);
	formatBadge->setObjectName(QStringLiteral("RefFormatBadge"));
	formatBadge->setAttribute(Qt::WA_StyledBackground, true);
	formatBadge->setVisible(false);
	nameLayout->addWidget(formatBadge, 0, Qt::AlignVCenter);

	absBadge = new QLabel(QStringLiteral("ABS"), nameRow);
	absBadge->setObjectName(QStringLiteral("RefAbsBadge"));
	absBadge->setAttribute(Qt::WA_StyledBackground, true);
	absBadge->setToolTip(tr("Absolute path - this reference does not move with the configuration"));
	absBadge->setVisible(false);
	nameLayout->addWidget(absBadge, 0, Qt::AlignVCenter);

	missingBadge = new QLabel(tr("MISSING"), nameRow);
	missingBadge->setObjectName(QStringLiteral("RefMissingBadge"));
	missingBadge->setAttribute(Qt::WA_StyledBackground, true);
	missingBadge->setVisible(false);
	nameLayout->addWidget(missingBadge, 0, Qt::AlignVCenter);

	nameLayout->addStretch(1);
	textLayout->addWidget(nameRow);

	dirLabel = new ElidedLabel(textBlock);
	dirLabel->setObjectName(QStringLiteral("RefDir"));
	dirLabel->setVisible(false);
	textLayout->addWidget(dirLabel);

	readoutLabel = new QLabel(textBlock);
	readoutLabel->setObjectName(QStringLiteral("RefReadout"));
	readoutLabel->setVisible(false);
	textLayout->addWidget(readoutLabel);

	statusLabel = new QLabel(textBlock);
	statusLabel->setObjectName(QStringLiteral("RefStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	textLayout->addWidget(statusLabel);

	rootLayout->addWidget(textBlock);

	// The action buttons follow the content instead of riding the card's
	// right edge (the header-control rule applied to the body: the eye
	// should not have to travel across a wide window). The trailing
	// stretch owns the leftover width.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	rootLayout->addLayout(actionLayout);
	rootLayout->addStretch(1);

	// Neutral styling straight from the tokens, so the default stays legible
	// under any skin that has not supplied its own view.
	const SkinTokens& tk = SkinManager::instance()->tokens();
	nameLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-weight: 600; }")
		.arg(tk.text));
	dirLabel->setStyleSheet(QStringLiteral("color: %1; font-family: \"%2\"; font-size: 8pt;")
		.arg(tk.mutedText, tk.monoFontFamily));
	readoutLabel->setStyleSheet(QStringLiteral("color: %1; font-family: \"%2\"; font-size: 8pt;")
		.arg(tk.mutedText, tk.monoFontFamily));
	statusLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 8pt; }"
		"QLabel[severity=\"warning\"] { color: %2; }"
		"QLabel[severity=\"critical\"] { color: %3; }")
		.arg(tk.mutedText, tk.warning, tk.danger));
	formatBadge->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; border: 1px solid %2; border-radius: 3px; padding: 0 5px; font-size: 7pt; font-weight: 700; }")
		.arg(tk.mutedText, tk.border));
	absBadge->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; border: 1px solid %1; border-radius: 3px; padding: 0 5px; font-size: 7pt; font-weight: 700; }")
		.arg(tk.warning));
	missingBadge->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; background: %2; border-radius: 3px; padding: 0 5px; font-size: 7pt; font-weight: 700; }")
		.arg(tk.background, tk.danger));
}

void DefaultReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	Q_UNUSED(role);
	button->setParent(contentWidget());
	actionLayout->addWidget(button, 0, Qt::AlignTop);
}

void DefaultReferenceCardView::addLeadingWidget(QWidget* widget)
{
	widget->setParent(contentWidget());
	// After the icon, before the text block: the control reads as part of the
	// reference grammar ("<channel> <file>").
	rootLayout->insertWidget(1, widget, 0, Qt::AlignVCenter);
}

void DefaultReferenceCardView::applyState(const ReferenceCardState& state)
{
	const SkinTokens& tk = SkinManager::instance()->tokens();
	iconLabel->setPixmap(GUIHelper::tintedIcon(iconResourceFor(state),
		QColor(state.missing ? tk.warning : tk.mutedText), 20).pixmap(GUIHelper::scale(QSize(20, 20))));

	nameLabel->setText(state.name);
	nameLabel->setToolTip(state.fullPath);

	formatBadge->setVisible(!state.formatBadge.isEmpty());
	formatBadge->setText(state.formatBadge);
	absBadge->setVisible(state.absolutePath && !state.missing);
	missingBadge->setVisible(state.missing);

	dirLabel->setVisible(!state.directory.isEmpty());
	dirLabel->setFullText(state.locationPrefix());

	readoutLabel->setVisible(!state.readout.isEmpty());
	readoutLabel->setText(state.readout.join(QStringLiteral(" %1 ").arg(QChar(0x00B7))));

	statusLabel->setVisible(!state.statusText.isEmpty());
	statusLabel->setText(state.statusText);
	statusLabel->setProperty("severity", referenceCardSeverityName(state.statusSeverity));
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);

	// A labelled action (the host swaps Browse to "Locate..." while missing)
	// shows its text; icon-only buttons stay compact.
	for (QToolButton* button : contentWidget()->findChildren<QToolButton*>())
	{
		if (actionLayout->indexOf(button) >= 0)
			button->setToolButtonStyle(button->text().isEmpty()
				? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
	}
}
