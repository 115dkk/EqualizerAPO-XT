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
// Which token seeds each kind's tile pastel. The hues stay inside the token
// family: Include leans on the accent blue, VST on the accent2 violet,
// Convolution on the success green, and MultiConvolution on a green/blue mix
// so the two convolution siblings read as relatives that still tell apart.
QColor kindTilePastel(const QString& kind, const SkinTokens& t, bool dark)
{
	if (kind == QStringLiteral("vst"))
		return softPastelize(QColor(t.accent2), dark);
	if (kind == QStringLiteral("convolution"))
		return softPastelize(QColor(t.success), dark);
	if (kind == QStringLiteral("multiconvolution"))
		return softPastelize(mixColor(QColor(t.success), QColor(t.accent), 0.45), dark);
	return softPastelize(QColor(t.accent), dark);
}

// The pictogram each kind wears (the shared modern icon set): the document
// sheet for Include, the plug for VST, the waveform for Convolution and the
// layered stack for MultiConvolution (its own mark: many impulse responses
// summed into one card). The missing-state stroke exclamation stays: the
// transition lives in the tile either way.
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

// The rounded-square colour tile that leads the row (the picker's tile
// grammar). While the reference is broken the tile changes colour and swaps
// the pictogram for a stroke-drawn alert mark; disabled, the pastel sinks
// toward the window background like every sleeping Soft chip.
class SoftReferenceTile : public QWidget
{
public:
	explicit SoftReferenceTile(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("SoftReferenceTile"));
		configurePaintOnlyChrome(this);
		setFixedSize(GUIHelper::scale(QSize(34, 34)));
	}

	void setAppearance(const QColor& pastel, const QString& newIconResource, bool alert)
	{
		tilePastel = pastel;
		iconResource = newIconResource;
		showAlert = alert;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		const SkinTokens& t = SkinManager::instance()->tokens();
		const qreal side = qMin(width(), height());
		QRectF tileRect((width() - side) / 2.0, (height() - side) / 2.0, side, side);
		tileRect.adjust(1.0, 1.0, -1.0, -1.0);

		// The glyph wears the picker tiles' near-white ink; the sleeping
		// state uses the typeBadgeStyle recipe (pastel sunk most of the way
		// into the window background, muted ink).
		QColor fill = tilePastel;
		QColor ink(QStringLiteral("#FAFAFC"));
		if (!isEnabled())
		{
			fill = mixColor(tilePastel, QColor(t.background), 0.62);
			ink = QColor(t.mutedText);
		}

		painter.setPen(Qt::NoPen);
		painter.setBrush(fill);
		// The picker tiles' 32% corner radius.
		painter.drawRoundedRect(tileRect, side * 0.32, side * 0.32);

		if (showAlert)
		{
			// A stroke-drawn exclamation mark (round caps, no icon font) -
			// the same hand as the picker's stroke magnifier.
			const QPointF center = tileRect.center();
			painter.setPen(QPen(ink, side * 0.09, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(center.x(), tileRect.top() + side * 0.26),
				QPointF(center.x(), tileRect.top() + side * 0.58));
			painter.setPen(Qt::NoPen);
			painter.setBrush(ink);
			const qreal dotRadius = side * 0.055;
			painter.drawEllipse(QPointF(center.x(), tileRect.top() + side * 0.74), dotRadius, dotRadius);
		}
		else if (!iconResource.isEmpty())
		{
			// The pictogram in the tile ink, centred at the picker glyphs'
			// optical share of the tile.
			const int glyphSide = qMax(1, qRound(side * 0.56));
			const QRect glyphRect(qRound(tileRect.center().x() - glyphSide / 2.0),
				qRound(tileRect.center().y() - glyphSide / 2.0), glyphSide, glyphSide);
			GUIHelper::tintedIcon(iconResource, ink, glyphSide).paint(&painter, glyphRect);
		}
	}

private:
	QColor tilePastel;
	QString iconResource;
	bool showAlert = false;
};

SoftReferenceCardView::SoftReferenceCardView(const QString& kind, QWidget* parent)
	: ReferenceCardView(parent), cardKind(kind)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(t);

	QWidget* page = contentWidget();
	rootLayout = new QHBoxLayout(page);
	// Roomy by constitution: whitespace is the hierarchy device.
	rootLayout->setContentsMargins(GUIHelper::scale(2.0), GUIHelper::scale(6.0),
		GUIHelper::scale(2.0), GUIHelper::scale(6.0));
	rootLayout->setSpacing(GUIHelper::scale(12.0));

	tile = new SoftReferenceTile(page);
	rootLayout->addWidget(tile, 0, Qt::AlignVCenter);

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

	rootLayout->addWidget(textColumn, 1);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(GUIHelper::scale(6.0));
	rootLayout->addLayout(actionLayout);

	nameLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 11pt; font-weight: 600; background: transparent; }"
		"QLabel:disabled { color: %2; font-weight: 500; }")
		.arg(t.text, t.mutedText));

	captionLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 9pt; background: transparent; }"
		"QLabel:disabled { color: %2; }")
		.arg(t.mutedText, cssColor(withAlpha(QColor(t.mutedText), 150))));

	// Fact chips: one quiet blue-grey pastel (the accent pulled toward the
	// muted ink before pastelizing - facts inform, they do not announce)
	// under the skin's deep warm chip ink (white on a pastel is low-contrast
	// anxiety). Sleeping chips sink toward the window like the type chip
	// does.
	const QColor chipPastel = softPastelize(mixColor(QColor(t.accent), QColor(t.mutedText), 0.55), dark);
	chipStyle = QStringLiteral(
		"QLabel { background: %1; color: %2; border-radius: 9px; padding: 2px 10px;"
		" font-size: 8pt; font-weight: 600; }"
		"QLabel:disabled { background: %3; color: %4; }")
		.arg(chipPastel.name(), QStringLiteral("#2B251D"),
			cssColor(mixColor(chipPastel, QColor(t.background), 0.62)), t.mutedText);
	formatChip->setStyleSheet(chipStyle);

	// Status stays a caption, not an alarm: severity inks are the warning /
	// danger hues mixed well toward the body text so they inform without
	// shouting (a red text wall is exactly what this skin removes).
	statusLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; font-size: 9pt; background: transparent; }"
		"QLabel[severity=\"warning\"] { color: %2; }"
		"QLabel[severity=\"critical\"] { color: %3; }"
		"QLabel:disabled { color: %1; }")
		.arg(t.mutedText,
			cssColor(mixColor(QColor(t.warning), QColor(t.text), dark ? 0.40 : 0.35)),
			cssColor(mixColor(QColor(t.danger), QColor(t.text), dark ? 0.40 : 0.35))));

	// The guided-recovery entry: while the host relabels Browse to a
	// translated "Locate...", the button becomes the row's protagonist - an
	// accent-pastel stadium pill (this skin's friendly primary), never a red
	// alarm. Disabled it sleeps on the tray surface.
	const QColor accent(t.accent);
	const QColor locateInk = mixColor(accent, QColor(t.text), dark ? 0.45 : 0.25);
	locatePillStyle = QStringLiteral(
		"QToolButton { background: %1; color: %2; border: 1px solid %3; border-radius: 15px;"
		" padding: 4px 14px; min-height: 22px; font-weight: 700; }"
		"QToolButton:hover { background: %4; }")
		.arg(cssColor(withAlpha(accent, dark ? 46 : 36)), cssColor(locateInk),
			cssColor(withAlpha(accent, dark ? 90 : 76)), cssColor(withAlpha(accent, dark ? 66 : 52)))
		+ QStringLiteral(
		"QToolButton:pressed { background: %1; }"
		"QToolButton:disabled { background: %2; color: %3; border-color: %4; }")
		.arg(cssColor(withAlpha(accent, dark ? 84 : 66)), t.surface, t.mutedText, t.border);
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
	// it stays an honest combo dressed as a stadium pill one value step
	// above the body tray with its arrow visible; disabled it keeps only a
	// dashed outline - a sleeping slot, not an alarm. The inner line edit
	// rides flat inside the pill.
	const SkinTokens& t = SkinManager::instance()->tokens();
	widget->setStyleSheet(QStringLiteral(
		"QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 13px;"
		" padding: 2px 18px 2px 10px; min-height: 22px; font-weight: 600; }"
		"QComboBox:hover { background: %4; }"
		"QComboBox:focus, QComboBox:on { border-color: %5; }"
		"QComboBox:disabled { color: %6; background: %7; border: 1px dashed %3; }"
		"QComboBox QLineEdit { background: transparent; border: 0; padding: 0; }")
		.arg(t.card, t.text, t.border)
		.arg(t.cardHover, t.accent, t.mutedText, t.surface));

	// Between the tile and the name: the channel is part of the reference
	// phrase ("<channel> <file>"), so it reads before the identity, not
	// among the trailing actions.
	rootLayout->insertWidget(1, widget, 0, Qt::AlignVCenter);
}

void SoftReferenceCardView::applyState(const ReferenceCardState& state)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(t);
	const QString kind = state.kind.isEmpty() ? cardKind : state.kind;

	// The broken-state transition lives in the tile, still on the pastel
	// shelf: an empty reference ("no file selected") is a quiet warning tint
	// - nothing broke yet - while a dangling path leans on the danger hue,
	// pastelized so it worries without alarming.
	QColor pastel = kindTilePastel(kind, t, dark);
	if (state.missing)
		pastel = softPastelize(QColor(state.editText.trimmed().isEmpty() ? t.warning : t.danger), dark);
	tile->setAppearance(pastel, kindIconResource(kind), state.missing);

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
// individual pastel stadium chips - the readout idiom of a skin whose taboo
// is a monospace fact line.
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
