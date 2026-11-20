/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The file-reference rows (Include, Convolution, MultiConvolution, VST) in
// the soft skin (docs/skins/soft.md). A row is what a file row is in any
// desktop program: the file's name, a quieter line saying where it is, a few
// facts as outlined tags, and the buttons right after the text. A problem is
// said once, by a lamp before the name and one sentence under it; nothing is
// tinted and there is no badge.

#include "SoftReferenceCardView.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/soft/SoftPaint.h"
#include "Editor/widgets/ElidedLabel.h"

// The state lamp before the name. It exists only while there is a state to
// report: a reference that resolves shows no mark at all, because the card's
// header already carries the command's pictogram.
class SoftReferenceTile : public QWidget
{
public:
	explicit SoftReferenceTile(const SkinTokens& tokens, QWidget* parent = nullptr)
		: QWidget(parent), skinTokens(tokens)
	{
		setObjectName(QStringLiteral("SoftReferenceTile"));
		configurePaintOnlyChrome(this);
		setFixedSize(QSize(12, 22));
		setVisible(false);
	}

	const SkinTokens skinTokens;

	void setLamp(const QColor& color, bool shown)
	{
		lampColor = color;
		setVisible(shown);
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		if (!isEnabled())
			painter.setOpacity(SoftPaint::disabledOpacity);
		SoftPaint::lamp(painter, QPointF(width() / 2.0, height() / 2.0 + 0.5), lampColor);
	}

private:
	QColor lampColor;
};

SoftReferenceCardView::SoftReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent)
	: ReferenceCardView(parent), skinTokens(tokens), cardKind(kind)
{
	const SkinTokens& t = skinTokens;

	QWidget* page = contentWidget();
	rootLayout = new QHBoxLayout(page);
	rootLayout->setContentsMargins(2, 6, 2, 6);
	rootLayout->setSpacing(10);

	// The identity column takes only the width of its text; the trailing
	// stretch (added last) absorbs the rest, so the buttons sit right after
	// the text they act on instead of at the far edge of a wide row.
	QWidget* textColumn = new QWidget(page);
	QVBoxLayout* textLayout = new QVBoxLayout(textColumn);
	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->setSpacing(2);

	QWidget* nameRow = new QWidget(textColumn);
	QHBoxLayout* nameLayout = new QHBoxLayout(nameRow);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	nameLayout->setSpacing(8);
	// The state lamp leads the name, on the name's own line.
	tile = new SoftReferenceTile(skinTokens, nameRow);
	nameLayout->addWidget(tile, 0, Qt::AlignVCenter);
	nameLabel = new ElidedLabel(nameRow);
	nameLabel->setObjectName(QStringLiteral("SoftRefName"));
	nameLabel->setElideMode(Qt::ElideRight);
	installNameActivation(nameLabel);
	nameLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);
	formatChip = new QLabel(nameRow);
	formatChip->setObjectName(QStringLiteral("SoftRefFormatChip"));
	formatChip->setAttribute(Qt::WA_StyledBackground, true);
	formatChip->setVisible(false);
	nameLayout->addWidget(formatChip, 0, Qt::AlignVCenter);
	nameLayout->addStretch(1);
	textLayout->addWidget(nameRow);

	captionLabel = new ElidedLabel(textColumn);
	captionLabel->setObjectName(QStringLiteral("SoftRefCaption"));
	captionLabel->setVisible(false);
	textLayout->addWidget(captionLabel);

	chipRow = new QWidget(textColumn);
	chipLayout = new QHBoxLayout(chipRow);
	chipLayout->setContentsMargins(0, 2, 0, 0);
	chipLayout->setSpacing(6);
	chipRow->setVisible(false);
	textLayout->addWidget(chipRow);

	statusLabel = new QLabel(textColumn);
	statusLabel->setObjectName(QStringLiteral("SoftRefStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	textLayout->addWidget(statusLabel);

	rootLayout->addWidget(textColumn);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(6, 0, 0, 0);
	actionLayout->setSpacing(8);
	rootLayout->addLayout(actionLayout);
	rootLayout->addStretch(1);

	nameLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 15px; font-weight: 600; background: transparent; }"
		"QLabel[refChecking=\"true\"] { color: %2; }"
		"QLabel:disabled { color: %2; }")
		.arg(t.text, t.mutedText));
	captionLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 13px; background: transparent; }").arg(t.mutedText));

	// A fact about the file is a tag: an outline and the body ink, never a
	// fill.
	chipStyle = QStringLiteral(
		"QLabel { background: transparent; color: %1; border: 1px solid %2; border-radius: 9px;"
		" padding: 1px 8px; min-height: 16px; font-size: 13px; font-weight: 500; }"
		"QLabel:disabled { color: %3; }")
		.arg(t.text, t.borderStrong, t.mutedText);
	formatChip->setStyleSheet(chipStyle);

	// The sentence under the name is in the body ink whatever the state: the
	// lamp carries the state colour, the sentence carries the words.
	statusLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 13px; background: transparent; }"
		"QLabel[severity=\"warning\"], QLabel[severity=\"critical\"] { color: %2; }"
		"QLabel:disabled { color: %1; }")
		.arg(t.mutedText, t.text));

	// A reference that does not resolve makes "locate it" the row's one
	// filled action. It cannot be taken on a switched-off row, and then it is
	// an outline, so a dimmed fill never reads as a tinted area.
	const QColor accent(t.accent);
	const QColor onAccent(t.onAccent);
	locatePillStyle = QStringLiteral(
		"QToolButton { background: %1; color: %2; border: 1px solid %1; border-radius: 6px;"
		" padding: 4px 14px; min-height: 22px; font-weight: 600; }"
		"QToolButton:hover { background: %3; border-color: %3; }"
		"QToolButton:pressed { background: %4; border-color: %4; }"
		"QToolButton:disabled { background: transparent; color: %5; border: 1px solid %6; }")
		.arg(t.accent, t.onAccent, cssColor(mixColor(accent, onAccent, 0.16)),
			cssColor(mixColor(accent, onAccent, 0.28)), t.mutedText, t.borderStrong);
}

void SoftReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	Q_UNUSED(role);
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
		toolButton->setAutoRaise(false);
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::addLeadingWidget(QWidget* widget)
{
	// The leading selector is an ordinary select; the sheets' QComboBox rule
	// dresses it like every other one.
	widget->setParent(contentWidget());
	rootLayout->insertWidget(0, widget, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::placeBusStrip(QWidget* strip)
{
	// Mid-row between the identity column and the buttons.
	strip->setParent(contentWidget());
	rootLayout->insertWidget(rootLayout->indexOf(actionLayout), strip, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::applyState(const ReferenceCardState& state)
{
	const SkinTokens& t = skinTokens;

	// The lamp: failure for a reference that is gone or a critical state,
	// "needs a look" for an empty reference or a warning. A file still being
	// looked for has no state yet and shows none.
	bool lamp = false;
	QColor lampColor(t.danger);
	if (!state.checking)
	{
		if (state.missing)
		{
			lamp = true;
			if (state.editText.trimmed().isEmpty())
				lampColor = QColor(t.warning);
		}
		else if (state.statusSeverity == ReferenceCardState::Severity::Critical)
			lamp = true;
		else if (state.statusSeverity == ReferenceCardState::Severity::Warning)
		{
			lamp = true;
			lampColor = QColor(t.warning);
		}
	}
	tile->setLamp(lampColor, lamp);

	nameLabel->setFullText(state.name);
	if (!state.fullPath.isEmpty())
		nameLabel->setToolTip(state.fullPath);
	if (nameLabel->property("refChecking").toBool() != state.checking)
	{
		nameLabel->setProperty("refChecking", state.checking);
		repolish(nameLabel);
	}

	// The loaded format (VST2 / VST3) is a tag beside the name.
	formatChip->setVisible(!state.formatBadge.isEmpty());
	formatChip->setText(state.formatBadge);

	// The location line: the containing folder; for a reference that does
	// not resolve, the reference as it is written.
	QString caption = state.locationPrefix();
	if (caption.isEmpty() && state.missing)
		caption = state.editText;
	if (caption == state.name)
		caption.clear();
	if (state.checking)
		caption = tr("Checking...");
	captionLabel->setVisible(!caption.isEmpty());
	captionLabel->setFullText(caption);
	if (!state.fullPath.isEmpty())
		captionLabel->setToolTip(state.fullPath);

	rebuildChips(state.readout);

	statusLabel->setVisible(!state.statusText.isEmpty());
	statusLabel->setText(state.statusText);
	statusLabel->setProperty("severity", referenceCardSeverityName(state.statusSeverity));
	repolish(statusLabel);

	styleBrowseButton();
}

void SoftReferenceCardView::rebuildChips(const QStringList& readout)
{
	while (QLayoutItem* item = chipLayout->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	for (const QString& fact : readout)
	{
		QLabel* chip = new QLabel(fact, chipRow);
		chip->setAttribute(Qt::WA_StyledBackground, true);
		chip->setStyleSheet(chipStyle);
		chipLayout->addWidget(chip, 0, Qt::AlignVCenter);
	}
	chipLayout->addStretch(1);
	chipRow->setVisible(!readout.isEmpty());
}

void SoftReferenceCardView::styleBrowseButton()
{
	QAbstractButton* browse = actionButton(ActionRole::Browse);
	if (browse == nullptr)
		return;

	const bool locate = locateMode();
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(browse))
		toolButton->setToolButtonStyle(locate ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	browse->setStyleSheet(locate ? locatePillStyle : QString());
}
