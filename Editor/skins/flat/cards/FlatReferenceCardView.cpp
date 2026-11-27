/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The file-reference rows (Include, Convolution, MultiConvolution, VST) in
// the flat skin (docs/skins/flat.md). A row is what a file row is in any
// desktop program: the file's name, a quieter line saying where it is, a few
// facts as outlined tags, and the buttons right after the text. A problem is
// said once, by a lamp before the name and one sentence under it; nothing is
// tinted and there is no badge.

#include "FlatReferenceCardView.h"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/flat/FlatPaint.h"
#include "Editor/widgets/ElidedLabel.h"

// The state lamp before the name. It exists only while there is a state to
// report: a reference that resolves shows no mark at all, because the card's
// header already carries the command's pictogram.
class FlatReferenceTile : public QWidget
{
public:
	explicit FlatReferenceTile(const SkinTokens& tokens, QWidget* parent = nullptr)
		: QWidget(parent), skinTokens(tokens)
	{
		setObjectName(QStringLiteral("FlatReferenceTile"));
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
			painter.setOpacity(FlatPaint::disabledOpacity);
		FlatPaint::lamp(painter, QPointF(width() / 2.0, height() / 2.0 + 0.5), lampColor);
	}

private:
	QColor lampColor;
};

FlatReferenceCardView::FlatReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent)
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
	textColumn = new QWidget(page);
	QVBoxLayout* textLayout = new QVBoxLayout(textColumn);
	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->setSpacing(2);

	QWidget* nameRow = new QWidget(textColumn);
	nameLayout = new QHBoxLayout(nameRow);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	nameLayout->setSpacing(8);
	// The state lamp leads the name, on the name's own line.
	tile = new FlatReferenceTile(skinTokens, nameRow);
	nameLayout->addWidget(tile, 0, Qt::AlignVCenter);
	nameLabel = new ElidedLabel(nameRow);
	nameLabel->setObjectName(QStringLiteral("FlatRefName"));
	nameLabel->setElideMode(Qt::ElideRight);
	installNameActivation(nameLabel);
	nameLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);
	formatChip = new QLabel(nameRow);
	formatChip->setObjectName(QStringLiteral("FlatRefFormatChip"));
	formatChip->setAttribute(Qt::WA_StyledBackground, true);
	formatChip->setVisible(false);
	nameLayout->addWidget(formatChip, 0, Qt::AlignVCenter);
	nameLayout->addStretch(1);
	textLayout->addWidget(nameRow);

	captionLabel = new ElidedLabel(textColumn);
	captionLabel->setObjectName(QStringLiteral("FlatRefCaption"));
	captionLabel->setVisible(false);
	textLayout->addWidget(captionLabel);

	chipRow = new QWidget(textColumn);
	chipLayout = new QHBoxLayout(chipRow);
	chipLayout->setContentsMargins(0, 2, 0, 0);
	chipLayout->setSpacing(6);
	chipRow->setVisible(false);
	textLayout->addWidget(chipRow);

	statusLabel = new QLabel(textColumn);
	statusLabel->setObjectName(QStringLiteral("FlatRefStatus"));
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

void FlatReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	Q_UNUSED(role);
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
		toolButton->setAutoRaise(false);
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void FlatReferenceCardView::addLeadingWidget(QWidget* widget)
{
	// The leading selector is an ordinary select; the sheets' QComboBox rule
	// dresses it like every other one.
	widget->setParent(contentWidget());
	rootLayout->insertWidget(0, widget, 0, Qt::AlignVCenter);
}

void FlatReferenceCardView::placeBusStrip(QWidget* strip)
{
	busStrip = strip;
	strip->setParent(contentWidget());
	if (rowMode && wiringLayout != nullptr)
	{
		strip->setSizePolicy(QSizePolicy::Expanding, strip->sizePolicy().verticalPolicy());
		wiringLayout->addWidget(strip, 1, Qt::AlignVCenter);
		keepReadoutWhole();
		return;
	}
	// Mid-row between the identity column and the buttons.
	rootLayout->insertWidget(rootLayout->indexOf(actionLayout), strip, 0, Qt::AlignVCenter);
}

void FlatReferenceCardView::setInlineRow(bool inlineRow)
{
	if (!inlineRow || rowMode)
		return;
	rowMode = true;
	keepNameCharacters();
	if (cardKind != QStringLiteral("vst"))
		return;

	// Name, tag, path, bus strip and buttons on one line ran past a 960 px
	// row (the readout and the name were cut). In a row the unit is two
	// lines: what it is and its buttons, right after the name (rule 9), and
	// under it where it points and how it is wired. The path is the one part
	// that elides; the strip keeps its whole width.
	QVBoxLayout* textLayout = qobject_cast<QVBoxLayout*>(textColumn->layout());
	if (textLayout == nullptr || nameLayout == nullptr)
		return;
	rootLayout->removeItem(actionLayout);
	nameLayout->insertLayout(nameLayout->count() - 1, actionLayout);

	wiringLayout = new QHBoxLayout();
	wiringLayout->setContentsMargins(0, 0, 0, 0);
	wiringLayout->setSpacing(10);
	textLayout->insertLayout(textLayout->indexOf(captionLabel) + 1, wiringLayout);
	textLayout->removeWidget(captionLabel);
	// The path takes its text width and is the first to give way; the strip
	// follows it (rule 9) and the end of the line keeps what is left.
	captionLabel->setSizePolicy(QSizePolicy::Preferred, captionLabel->sizePolicy().verticalPolicy());
	wiringLayout->addWidget(captionLabel, 0, Qt::AlignVCenter);
	// The strip never shrinks below its hint, and it takes the rest of the
	// line: its readout area is whatever the strip has after the selects,
	// and the hint reserves it with a narrower face than the readout is
	// painted in, so a strip held to its hint cut "Stereo -> Stereo".
	if (busStrip != nullptr)
	{
		rootLayout->removeWidget(busStrip);
		busStrip->setSizePolicy(QSizePolicy::Expanding, busStrip->sizePolicy().verticalPolicy());
		wiringLayout->addWidget(busStrip, 1, Qt::AlignVCenter);
	}
	// The strip's hint changes with its readout; the page hears of it as a
	// layout request and gives the strip its readout slack again.
	contentWidget()->installEventFilter(this);
	keepReadoutWhole();

	// The text column now holds the buttons, so it takes the row's width
	// and the trailing stretch gives it up.
	rootLayout->setStretchFactor(textColumn, 1);
	for (int i = 0; i < rootLayout->count(); i++)
	{
		if (QSpacerItem* spacer = rootLayout->itemAt(i)->spacerItem())
		{
			spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
			rootLayout->setStretch(i, 0);
		}
	}
	rootLayout->invalidate();
}

void FlatReferenceCardView::keepReadoutWhole()
{
	// The strip reserves its readout with a narrower face than the 13 px
	// caption face this skin paints it in (FlatSkin::paintVstBusFrame), and
	// its readout area is what the strip has after the selects. Held to its
	// hint, the strip cut "Stereo -> Stereo" (in Korean the path took the
	// rest of the line). The slack is that difference for a pair of layout
	// names, so the path, not the readout, gives way.
	if (!rowMode || busStrip == nullptr)
		return;
	constexpr int readoutSlack = 40;
	const int wanted = busStrip->sizeHint().width() + readoutSlack;
	if (busStrip->minimumWidth() != wanted)
		busStrip->setMinimumWidth(wanted);
}

bool FlatReferenceCardView::eventFilter(QObject* watched, QEvent* event)
{
	if (rowMode && watched == contentWidget() && event->type() == QEvent::LayoutRequest)
		keepReadoutWhole();
	return ReferenceCardView::eventFilter(watched, event);
}

void FlatReferenceCardView::keepNameCharacters()
{
	// The display clause of a row: the name keeps its first 12 characters
	// before it elides, and its whole text when that is shorter.
	if (!rowMode)
		return;
	nameLabel->ensurePolished();
	const QString name = nameLabel->property("flatFullName").toString();
	const QFontMetrics metrics = nameLabel->fontMetrics();
	const int whole = metrics.horizontalAdvance(name);
	const int kept = metrics.horizontalAdvance(name.left(12) + QChar(0x2026));
	nameLabel->setMinimumWidth(qMin(whole, kept) + 2);
}

void FlatReferenceCardView::applyState(const ReferenceCardState& state)
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
	nameLabel->setProperty("flatFullName", state.name);
	keepNameCharacters();
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

void FlatReferenceCardView::rebuildChips(const QStringList& readout)
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

void FlatReferenceCardView::styleBrowseButton()
{
	QAbstractButton* browse = actionButton(ActionRole::Browse);
	if (browse == nullptr)
		return;

	const bool locate = locateMode();
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(browse))
		toolButton->setToolButtonStyle(locate ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	browse->setStyleSheet(locate ? locatePillStyle : QString());
}
