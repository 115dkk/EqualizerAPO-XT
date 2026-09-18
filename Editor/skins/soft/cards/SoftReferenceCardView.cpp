/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SoftReferenceCardView.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
// The pictogram each kind wears (the shared modern icon set): the document
// sheet for Include, the plug for VST, the waveform for Convolution and the
// layered stack for MultiConvolution (its own mark: many impulse responses
// summed into one card). The pictogram alone tells the kinds apart since
// the concept swap - the colour tiles retired, colour being for what is
// chosen or needs attention. The missing-state stroke exclamation stays:
// the transition lives in the glyph either way.
QString kindIconResource(const QString& kind)
{
	if (kind == QStringLiteral("vst"))
		return QStringLiteral(":/icons/modern/plugin.svg");
	if (kind == QStringLiteral("include"))
		return QStringLiteral(":/icons/modern/file-include.svg");
	if (kind == QStringLiteral("multiconvolution"))
		return QStringLiteral(":/icons/modern/multi-convolution.svg");
	return QStringLiteral(":/icons/modern/waveform.svg");
}
}

// The stroke pictogram that leads the row, in ink on the card paper -
// the same 34px slot the colour tile used to fill, so nothing moves.
// While the reference is broken the glyph turns to the amber ink and
// swaps the pictogram for a stroke-drawn alert mark; disabled, it relaxes
// to the muted ink like every sleeping Soft mark.
class SoftReferenceGlyph : public QWidget
{
public:
	explicit SoftReferenceGlyph(const SkinTokens& tokens, QWidget* parent = nullptr)
		: QWidget(parent), skinTokens(tokens)
	{
		setObjectName(QStringLiteral("SoftReferenceGlyph"));
		configurePaintOnlyChrome(this);
		setFixedSize(GUIHelper::scale(QSize(34, 34)));
	}

	const SkinTokens skinTokens;

	void setAppearance(const QColor& newInk, const QString& newIconResource, bool alert)
	{
		glyphInk = newInk;
		iconResource = newIconResource;
		showAlert = alert;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		const SkinTokens& t = skinTokens;
		const qreal side = qMin(width(), height());
		QRectF slot((width() - side) / 2.0, (height() - side) / 2.0, side, side);
		slot.adjust(1.0, 1.0, -1.0, -1.0);

		const QColor ink = isEnabled() ? glyphInk : QColor(t.mutedText);

		if (showAlert)
		{
			// A stroke-drawn exclamation mark (round caps, no icon font) -
			// the same hand as the picker's stroke magnifier.
			const QPointF center = slot.center();
			painter.setPen(QPen(ink, side * 0.09, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(center.x(), slot.top() + side * 0.22),
				QPointF(center.x(), slot.top() + side * 0.58));
			painter.setPen(Qt::NoPen);
			painter.setBrush(ink);
			const qreal dotRadius = side * 0.06;
			painter.drawEllipse(QPointF(center.x(), slot.top() + side * 0.76), dotRadius, dotRadius);
		}
		else if (!iconResource.isEmpty())
		{
			// The bare pictogram, larger than it was inside the tile: it is
			// the whole mark now.
			const int glyphSide = qMax(1, qRound(side * 0.76));
			const QRect glyphRect(qRound(slot.center().x() - glyphSide / 2.0),
				qRound(slot.center().y() - glyphSide / 2.0), glyphSide, glyphSide);
			GUIHelper::tintedIcon(iconResource, ink, glyphSide).paint(&painter, glyphRect);
		}
	}

private:
	QColor glyphInk;
	QString iconResource;
	bool showAlert = false;
};

SoftReferenceCardView::SoftReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent)
	: ReferenceCardView(parent), skinTokens(tokens), cardKind(kind)
{
	const SkinTokens& t = skinTokens;

	QWidget* page = contentWidget();
	rootLayout = new QHBoxLayout(page);
	// Roomy by constitution: whitespace is the hierarchy device.
	rootLayout->setContentsMargins(GUIHelper::scale(2.0), GUIHelper::scale(6.0),
		GUIHelper::scale(2.0), GUIHelper::scale(6.0));
	rootLayout->setSpacing(GUIHelper::scale(12.0));

	glyph = new SoftReferenceGlyph(skinTokens, page);
	rootLayout->addWidget(glyph, 0, Qt::AlignVCenter);

	QWidget* textColumn = new QWidget(page);
	QVBoxLayout* textLayout = new QVBoxLayout(textColumn);
	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->setSpacing(GUIHelper::scale(2.0));

	QWidget* nameRow = new QWidget(textColumn);
	QHBoxLayout* nameLayout = new QHBoxLayout(nameRow);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	nameLayout->setSpacing(GUIHelper::scale(8.0));

	// The identity line: the name in body ink at the card-title weight,
	// elided at paint time so a long plugin name can never push the row past
	// the 960px viewport.
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

	// The friendly second line this constitution alone allows: the location
	// as a muted body-face caption (never monospace), middle-elided at paint
	// time with the full path surviving in the tooltip.
	captionLabel = new ElidedLabel(textColumn);
	captionLabel->setObjectName(QStringLiteral("SoftRefCaption"));
	captionLabel->setVisible(false);
	textLayout->addWidget(captionLabel);

	chipRow = new QWidget(textColumn);
	chipLayout = new QHBoxLayout(chipRow);
	chipLayout->setContentsMargins(0, GUIHelper::scale(2.0), 0, 0);
	chipLayout->setSpacing(GUIHelper::scale(6.0));
	chipRow->setVisible(false);
	textLayout->addWidget(chipRow);

	statusLabel = new QLabel(textColumn);
	statusLabel->setObjectName(QStringLiteral("SoftRefStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	textLayout->addWidget(statusLabel);

	rootLayout->addWidget(textColumn);

	// The action buttons follow the text column instead of riding the
	// card's right edge; the stretch owns the leftover width.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(GUIHelper::scale(6.0));
	rootLayout->addLayout(actionLayout);
	rootLayout->addStretch(1);

	nameLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 11pt; font-weight: 600; background: transparent; }"
		"QLabel:disabled { color: %2; font-weight: 500; }")
		.arg(t.text, t.mutedText));

	captionLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 9pt; background: transparent; }"
		"QLabel:disabled { color: %2; }")
		.arg(t.mutedText, cssColor(withAlpha(QColor(t.mutedText), 150))));

	// Fact chips: resting pills (the well behind the light border) in the
	// muted ink - facts inform, they do not announce. Sleeping chips sink
	// to the window like every sleeping Soft mark.
	chipStyle = QStringLiteral(
		"QLabel { background: %1; color: %2; border: 1px solid %3; border-radius: 9px; padding: 2px 10px;"
		" font-size: 8pt; font-weight: 600; }"
		"QLabel:disabled { background: %4; color: %2; border: 1px dashed %3; }")
		.arg(t.surfaceSunken, t.mutedText, t.border, t.background);
	formatChip->setStyleSheet(chipStyle);

	// Status stays a caption, not an alarm: anything to look at speaks in
	// the one amber ink (a red text wall is exactly what this skin
	// removes, and since the concept swap there is no red at all).
	statusLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 9pt; background: transparent; }"
		"QLabel[severity=\"warning\"] { color: %2; }"
		"QLabel[severity=\"critical\"] { color: %2; }"
		"QLabel:disabled { color: %1; }")
		.arg(t.mutedText, t.warning));

	// The guided-recovery entry: while the host relabels Browse to a
	// translated "Locate...", the button becomes the row's protagonist -
	// the primary pill (the tint under accent ink, a full accent edge),
	// never a red alarm. Disabled it sleeps on the window.
	const QColor accent(t.accent);
	locatePillStyle = QStringLiteral(
		"QToolButton { background: %1; color: %2; border: 1px solid %2; border-radius: 15px;"
		" padding: 4px 14px; min-height: 22px; font-weight: 700; }"
		"QToolButton:hover { background: %3; }")
		.arg(t.cardSelected, t.accent, cssRgba(accent, 0.24))
		+ QStringLiteral(
		"QToolButton:pressed { background: %1; }"
		"QToolButton:disabled { background: %2; color: %3; border: 1px dashed %4; }")
		.arg(cssRgba(accent, 0.34), t.background,
			cssColor(withAlpha(QColor(t.mutedText), 140)), t.border);
}

void SoftReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	Q_UNUSED(role);
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
		toolButton->setAutoRaise(false);
	// Soft centres its controls in the roomy row instead of pinning them to
	// the top edge; the pill shapes come from the sheet's card-action rules
	// (and, for the Locate protagonist, from styleBrowseButton).
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::addLeadingWidget(QWidget* widget)
{
	widget->setParent(contentWidget());

	// MultiConvolution's output-channel selector is a genuine selector, so
	// it stays an honest combo dressed as a well pill with its arrow
	// visible; disabled it keeps only a dashed outline - a sleeping slot,
	// not an alarm. The inner line edit rides flat inside the pill.
	const SkinTokens& t = skinTokens;
	widget->setStyleSheet(QStringLiteral(
		"QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 13px;"
		" padding: 2px 18px 2px 10px; min-height: 22px; font-weight: 600; }"
		"QComboBox:hover { background: %4; }"
		"QComboBox:focus, QComboBox:on { border-color: %5; }"
		"QComboBox:disabled { color: %6; background: %7; border: 1px dashed %3; }"
		"QComboBox QLineEdit { background: transparent; border: 0; padding: 0; }")
		.arg(t.surfaceSunken, t.text, t.border)
		.arg(t.card, t.accent, t.mutedText, t.background));

	// Between the tile and the name: the channel is part of the reference
	// phrase ("<channel> <file>"), so it reads before the identity, not
	// among the trailing actions.
	rootLayout->insertWidget(1, widget, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::placeBusStrip(QWidget* strip)
{
	// Between the two-line identity column and the action pills: the chips
	// read as part of the plugin's description, and the pills follow them.
	strip->setParent(contentWidget());
	rootLayout->insertWidget(rootLayout->indexOf(actionLayout), strip, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::applyState(const ReferenceCardState& state)
{
	const SkinTokens& t = skinTokens;
	const QString kind = state.kind.isEmpty() ? cardKind : state.kind;

	// Problems live in the glyph - the one place this row already taught
	// to change state, so nothing new has to shout. A broken reference and
	// a warning/critical status line alike flip the stroke "!" in the
	// amber ink: a missing file, a rejected bus contract or stale VST2 keys
	// read from the picture first, and the caption below only has to
	// explain (r3 judging - a problem stated by long text alone was
	// disorienting). One attention ink since the concept swap: empty,
	// dangling and rejected are all things to look at, not three hues.
	const bool alert = state.missing
		|| state.statusSeverity == ReferenceCardState::Severity::Critical
		|| state.statusSeverity == ReferenceCardState::Severity::Warning;
	glyph->setAppearance(QColor(alert ? t.warning : t.text), kindIconResource(kind), alert);

	nameLabel->setFullText(state.name);
	if (!state.fullPath.isEmpty())
		nameLabel->setToolTip(state.fullPath);

	formatChip->setVisible(!state.formatBadge.isEmpty());
	formatChip->setText(state.formatBadge);

	// The caption is the friendly second line: the containing location as a
	// prefix ("Surround\"); while the reference dangles it shows the
	// reference as written, so the row itself explains what needs relinking.
	// No ABS badge in this skin (constitutional tiebreaker - the drive
	// letter this caption starts with already says it).
	QString caption = state.locationPrefix();
	if (caption.isEmpty() && state.missing)
		caption = state.editText;
	// A bare relative reference repeats the name - drop the echo, keep the air.
	if (caption == state.name)
		caption.clear();
	captionLabel->setVisible(!caption.isEmpty());
	captionLabel->setFullText(caption);
	if (!state.fullPath.isEmpty())
		captionLabel->setToolTip(state.fullPath);

	rebuildChips(state.readout);

	statusLabel->setVisible(!state.statusText.isEmpty());
	statusLabel->setText(state.statusText);
	statusLabel->setProperty("severity", referenceCardSeverityName(state.statusSeverity));
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);

	styleBrowseButton();
}

// Measured facts about the target ("100.0 ms", "48000 Hz", "2 ch") become
// individual resting pills - the readout idiom of a skin whose taboo is a
// monospace fact line.
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

	// The host swaps the Browse label to a translated "Locate..." while the
	// reference is broken. With a label present the pill becomes the visual
	// protagonist of the recovery; without one it rests as the quiet icon
	// pill the sheet gives every card action.
	const bool locate = locateMode();
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(browse))
		toolButton->setToolButtonStyle(locate ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	browse->setStyleSheet(locate ? locatePillStyle : QString());
}
