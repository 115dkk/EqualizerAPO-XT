/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See RackReferenceCardView.h.
*/

#include "RackReferenceCardView.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Engraved function caption over the label strip, per reference kind.
// Hardware printing, never translated (rack.md: engravings are not UI
// strings). The Include caption echoes the PATCH designation the ear
// engraving and the patchbay jacks already give that unit type.
QString captionForKind(const QString& kind)
{
	if (kind == QLatin1String("include"))
		return QStringLiteral("PATCH");
	if (kind == QLatin1String("vst"))
		return QStringLiteral("MODULE");
	return QStringLiteral("IR PROGRAM");
}
}

// ---- RackEngravedLabel -----------------------------------------------------

RackEngravedLabel::RackEngravedLabel(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent), skinTokens(tokens)
{
	// Preferred keeps the shrink flag, so elidable printing survives narrow
	// layouts instead of forcing a horizontal scrollbar (the 960px gate).
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void RackEngravedLabel::setText(const QString& newText)
{
	if (text == newText)
		return;
	text = newText;
	updateGeometry();
	update();
}

void RackEngravedLabel::setInk(Ink newInk)
{
	if (ink == newInk)
		return;
	ink = newInk;
	update();
}

void RackEngravedLabel::setDimmed(bool newDimmed)
{
	if (dimmed == newDimmed)
		return;
	dimmed = newDimmed;
	update();
}

void RackEngravedLabel::setHotTrack(bool newHotTrack)
{
	if (hotTrack == newHotTrack)
		return;
	hotTrack = newHotTrack;
	update();
}

void RackEngravedLabel::setPixelSize(int newPixelSize)
{
	pixelSize = newPixelSize;
	updateGeometry();
	update();
}

void RackEngravedLabel::setLetterSpacing(qreal newLetterSpacing)
{
	letterSpacing = newLetterSpacing;
	updateGeometry();
	update();
}

void RackEngravedLabel::setBoldFace(bool newBoldFace)
{
	boldFace = newBoldFace;
	updateGeometry();
	update();
}

void RackEngravedLabel::setStamped(bool newStamped)
{
	stamped = newStamped;
	updateGeometry();
	update();
}

void RackEngravedLabel::setElideMode(Qt::TextElideMode newElideMode)
{
	elideMode = newElideMode;
	updateGeometry();
	update();
}

QFont RackEngravedLabel::engraveFont() const
{
	// The letter-spaced DM Sans approximation of condensed engraving type
	// (rack.md), read from the live tokens at use time.
	QFont font(skinTokens.fontFamily);
	font.setPixelSize(pixelSize);
	font.setBold(boldFace);
	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
	return font;
}

QSize RackEngravedLabel::sizeHint() const
{
	const QFontMetrics metrics(engraveFont());
	int width = metrics.horizontalAdvance(text) + 2;
	int height = metrics.height() + 2;
	if (stamped)
	{
		width += 10;
		height += 2;
	}
	return QSize(width, height);
}

QSize RackEngravedLabel::minimumSizeHint() const
{
	QSize hint = sizeHint();
	// Elidable printing must not let the full text dictate the minimum.
	if (elideMode != Qt::ElideNone)
		hint.setWidth(qMin(hint.width(), GUIHelper::scale(28.0)));
	return hint;
}

void RackEngravedLabel::paintEvent(QPaintEvent*)
{
	if (text.isEmpty())
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const SkinTokens& tokens = skinTokens;
	const bool dark = skinIsDark(tokens);

	QColor bodyInk;
	switch (ink)
	{
	case Ink::Muted:
		bodyInk = QColor(tokens.mutedText);
		break;
	case Ink::Warning:
		bodyInk = QColor(tokens.warning);
		break;
	case Ink::Danger:
		bodyInk = QColor(tokens.danger);
		break;
	case Ink::Body:
	default:
		bodyInk = QColor(tokens.text);
		break;
	}
	// The clickable identity warms amber under the hand - hover is answered
	// by the lamp grammar, never by a button lift.
	if (hotTrack && isEnabled() && underMouse())
		bodyInk = QColor(tokens.accent);

	int alpha = 255;
	if (!isEnabled())
		alpha = 90;   // powered-down unit: the printing fades with the plate
	else if (dimmed)
		alpha = 140;  // missing reference: readable, but receded
	bodyInk.setAlpha(alpha);

	const QFont font = engraveFont();
	painter.setFont(font);

	QRectF textRect = QRectF(rect()).adjusted(1, 0, -1, -1);
	int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
	if (stamped)
	{
		textRect = QRectF(rect()).adjusted(5, 1, -5, -2);
		flags = Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine;
	}

	QString shown = text;
	if (elideMode != Qt::ElideNone)
		shown = QFontMetrics(font).elidedText(text, elideMode, int(textRect.width()));

	// Stamped tags are printed wireframe outlines - no fill; on this plate
	// colour lives only in lamps and engravings.
	if (stamped)
	{
		painter.setPen(QPen(withAlpha(bodyInk, qMin(alpha, 200)), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -1.5), 1, 1);
	}

	// Rack's engraved-text double pass: the recess edge catching the
	// work light, then the body ink on top.
	QColor recess = dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200);
	if (!isEnabled())
		recess.setAlpha(recess.alpha() / 2);
	painter.setPen(recess);
	painter.drawText(textRect.translated(0, 1), flags, shown);
	painter.setPen(bodyInk);
	painter.drawText(textRect, flags, shown);
}

void RackEngravedLabel::enterEvent(QEnterEvent* event)
{
	QWidget::enterEvent(event);
	if (hotTrack)
		update();
}

void RackEngravedLabel::leaveEvent(QEvent* event)
{
	QWidget::leaveEvent(event);
	if (hotTrack)
		update();
}

// ---- RackStatusLamp --------------------------------------------------------

RackStatusLamp::RackStatusLamp(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent)
	, skinTokens(tokens)
	, litColor(tokens.accent2)
{
	setFixedSize(GUIHelper::scale(QSize(20, 20)));
}

void RackStatusLamp::setLamp(const QColor& color, bool newLit)
{
	litColor = color;
	lit = newLit;
	update();
}

void RackStatusLamp::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const SkinTokens& tokens = skinTokens;
	const bool dark = skinIsDark(tokens);
	const QPointF center = QRectF(rect()).center();
	const qreal radius = qMin(width(), height()) / 2.0 - 4.5;
	// Disabled row = powered-down unit: the lamp is off, the hardware stays.
	const bool on = lit && isEnabled();

	// Rack's panel-lamp grammar: bezel ring, halo while lit, gradient
	// dome, specular dot.
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(70, 62, 50, 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (on)
	{
		const qreal haloRadius = qMin(width(), height()) / 2.0 - 0.5;
		QRadialGradient halo(center, haloRadius);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, haloRadius, haloRadius);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	if (on)
	{
		dome.setColorAt(0.0, litColor.lighter(150));
		dome.setColorAt(1.0, litColor.darker(125));
	}
	else
	{
		const QColor off = litColor.darker(330);
		dome.setColorAt(0.0, off.lighter(140));
		dome.setColorAt(1.0, off);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(dome);
	painter.drawEllipse(center, radius, radius);
	painter.setBrush(QColor(255, 255, 255, on ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

// ---- RackLcdWindow ---------------------------------------------------------

RackLcdWindow::RackLcdWindow(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent), skinTokens(tokens)
{
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	setMaximumWidth(GUIHelper::scale(320.0));
}

void RackLcdWindow::setSegments(const QString& newText)
{
	if (text == newText)
		return;
	text = newText;
	setToolTip(text);
	updateGeometry();
	update();
}

QFont RackLcdWindow::segmentFont() const
{
	QFont font(skinTokens.monoFontFamily);
	font.setPixelSize(10);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
	return font;
}

QString RackLcdWindow::displayText() const
{
	// Segment displays print caps; a case transform is presentation only, so
	// the host's (possibly translated) readout text stays untouched.
	return text.toUpper();
}

QSize RackLcdWindow::sizeHint() const
{
	const QFontMetrics metrics(segmentFont());
	return QSize(metrics.horizontalAdvance(displayText()) + 20, metrics.height() + 12);
}

QSize RackLcdWindow::minimumSizeHint() const
{
	return QSize(GUIHelper::scale(72.0), sizeHint().height());
}

void RackLcdWindow::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const SkinTokens& tokens = skinTokens;
	const bool dark = skinIsDark(tokens);

	// The display-window clause: an LCD set into the plate keeps its dark
	// glass in BOTH modes. Same glass and segment inks as the card's
	// EditableValue display (rack sheets).
	const QRectF well = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
	painter.setBrush(QColor(10, 14, 11));
	painter.drawRoundedRect(well, 2, 2);
	// Recessed depth: the glass top edge falls into the well's shadow, the
	// bottom bezel lip catches the work light (the valueScrub well's law).
	painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
	painter.drawLine(QPointF(well.left() + 2, well.top() + 1.5), QPointF(well.right() - 2, well.top() + 1.5));
	painter.setPen(QPen(dark ? QColor(0x39, 0x42, 0x4A) : QColor(0x6B, 0x63, 0x54), 1));
	painter.drawLine(QPointF(well.left() + 2.5, well.bottom()), QPointF(well.right() - 2.5, well.bottom()));

	if (text.isEmpty())
		return;

	QColor segmentInk = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
	if (!isEnabled())
		segmentInk = withAlpha(segmentInk, 70);  // powered-down display
	const QFont font = segmentFont();
	painter.setFont(font);
	const QFontMetrics metrics(font);
	const QRectF textRect = well.adjusted(9, 2, -9, -2);
	const QString shown = metrics.elidedText(displayText(), Qt::ElideRight, int(textRect.width()));
	painter.setPen(segmentInk);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, shown);
}

// ---- RackReferenceCardView -------------------------------------------------

RackReferenceCardView::RackReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent)
	: ReferenceCardView(parent), skinTokens(tokens)
{
	QWidget* page = contentWidget();
	rootLayout = new QHBoxLayout(page);
	rootLayout->setContentsMargins(0, 2, 0, 2);
	rootLayout->setSpacing(10);

	// The status lamp leads the face: on hardware the service lamp sits
	// beside the label strip, and state is read from lamps before words.
	lamp = new RackStatusLamp(skinTokens, page);
	rootLayout->addWidget(lamp, 0, Qt::AlignVCenter);

	// The engraved label strip: function caption + stamped tags over the
	// target's name, with the location and any service note as sub-printing.
	QWidget* labelArea = new QWidget(page);
	labelArea->setObjectName(QStringLiteral("RackRefLabelArea"));
	QVBoxLayout* labelLayout = new QVBoxLayout(labelArea);
	labelLayout->setContentsMargins(0, 0, 0, 0);
	labelLayout->setSpacing(1);

	QWidget* captionRow = new QWidget(labelArea);
	captionRow->setObjectName(QStringLiteral("RackRefCaptionRow"));
	QHBoxLayout* captionLayout = new QHBoxLayout(captionRow);
	captionLayout->setContentsMargins(0, 0, 0, 0);
	captionLayout->setSpacing(6);

	captionLabel = new RackEngravedLabel(skinTokens, captionRow);
	captionLabel->setPixelSize(8);
	captionLabel->setLetterSpacing(2.0);
	captionLabel->setInk(RackEngravedLabel::Ink::Muted);
	captionLabel->setText(captionForKind(kind));
	captionLayout->addWidget(captionLabel, 0, Qt::AlignVCenter);

	// No format stamp beside the caption: on a VST unit the loaded ABI is
	// engraved into the brass brand nameplate at the header's right edge
	// (RackSkin::paintCardChrome reads the row's posted format), and a
	// second wireframe tag crowding MODULE was judged cramped (r3).

	// The absolute-path hazard as a stamped wireframe tag in warning ink
	// (untranslated hardware marking).
	absStamp = new RackEngravedLabel(skinTokens, captionRow);
	absStamp->setPixelSize(8);
	absStamp->setLetterSpacing(1.0);
	absStamp->setInk(RackEngravedLabel::Ink::Warning);
	absStamp->setStamped(true);
	absStamp->setText(QStringLiteral("ABS"));
	absStamp->setVisible(false);
	captionLayout->addWidget(absStamp, 0, Qt::AlignVCenter);

	// The service condition caption (NOT FOUND / EMPTY SLOT): one small
	// engraving, never an alarm sentence across the plate.
	serviceTag = new RackEngravedLabel(skinTokens, captionRow);
	serviceTag->setPixelSize(8);
	serviceTag->setLetterSpacing(1.5);
	serviceTag->setInk(RackEngravedLabel::Ink::Warning);
	serviceTag->setVisible(false);
	captionLayout->addWidget(serviceTag, 0, Qt::AlignVCenter);

	captionLayout->addStretch(1);
	labelLayout->addWidget(captionRow);

	nameLabel = new RackEngravedLabel(skinTokens, labelArea);
	nameLabel->setPixelSize(13);
	nameLabel->setLetterSpacing(0.4);
	nameLabel->setElideMode(Qt::ElideMiddle);
	installNameActivation(nameLabel);
	labelLayout->addWidget(nameLabel);

	dirLabel = new RackEngravedLabel(skinTokens, labelArea);
	dirLabel->setPixelSize(10);
	dirLabel->setBoldFace(false);
	dirLabel->setInk(RackEngravedLabel::Ink::Muted);
	dirLabel->setElideMode(Qt::ElideMiddle);
	dirLabel->setVisible(false);
	labelLayout->addWidget(dirLabel);

	statusLabel = new RackEngravedLabel(skinTokens, labelArea);
	statusLabel->setPixelSize(10);
	statusLabel->setBoldFace(false);
	statusLabel->setElideMode(Qt::ElideRight);
	statusLabel->setVisible(false);
	labelLayout->addWidget(statusLabel);

	rootLayout->addWidget(labelArea, 0, Qt::AlignVCenter);

	// The impulse-response readout window sits between the label strip and
	// the machine buttons, where hardware mounts its meters.
	lcdWindow = new RackLcdWindow(skinTokens, page);
	lcdWindow->setVisible(false);
	rootLayout->addWidget(lcdWindow, 0, Qt::AlignVCenter);

	// The machine buttons mount right after the readout window, not on the
	// far end of the plate; the empty plate continues to the right.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	rootLayout->addLayout(actionLayout);
	rootLayout->addStretch(1);
}

void RackReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	Q_UNUSED(role);
	// Host order is display order; the caps mount centered on the plate.
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void RackReferenceCardView::placeBusStrip(QWidget* strip)
{
	// Where hardware mounts its meters: between the engraved label strip and
	// the machine buttons, the same seat the LCD readout takes on IR units.
	// The strip paints its own recessed sub-panel.
	strip->setParent(contentWidget());
	rootLayout->insertWidget(rootLayout->indexOf(lcdWindow), strip, 0, Qt::AlignVCenter);
}

void RackReferenceCardView::addLeadingWidget(QWidget* widget)
{
	widget->setParent(contentWidget());
	// Between the lamp and the label strip: the output selector is part of
	// the reference grammar ("<channel> <file>") and reads as a panel
	// selector ahead of the engraved label (QSS dresses it as the R2
	// engraved-selector treatment).
	rootLayout->insertWidget(1, widget, 0, Qt::AlignVCenter);
}

void RackReferenceCardView::applyState(const ReferenceCardState& state)
{
	const bool emptyRef = state.editText.trimmed().isEmpty();

	nameLabel->setText(state.name);
	nameLabel->setDimmed(state.missing);
	nameLabel->setHotTrack(state.nameClickable && !state.missing);
	nameLabel->setToolTip(state.fullPath);

	absStamp->setVisible(state.absolutePath && !state.missing);

	// The service condition: a broken reference engraves NOT FOUND; a
	// reference that was never set reads as a pulled module's empty slot.
	if (state.missing)
	{
		serviceTag->setText(emptyRef ? QStringLiteral("EMPTY SLOT") : QStringLiteral("NOT FOUND"));
		serviceTag->setInk(emptyRef ? RackEngravedLabel::Ink::Muted : RackEngravedLabel::Ink::Warning);
	}
	serviceTag->setVisible(state.missing);

	// Sub-printed as the containing prefix ("Surround\"): the folder holds
	// the unit's program, so it engraves as a path prefix, not as a sub-item
	// of the name above.
	dirLabel->setText(state.locationPrefix());
	dirLabel->setToolTip(state.directory);
	dirLabel->setVisible(!state.directory.isEmpty());

	statusLabel->setText(state.statusText);
	statusLabel->setToolTip(state.statusText);
	statusLabel->setInk(state.statusSeverity == ReferenceCardState::Severity::Critical
		? RackEngravedLabel::Ink::Danger
		: state.statusSeverity == ReferenceCardState::Severity::Warning
		? RackEngravedLabel::Ink::Warning
		: RackEngravedLabel::Ink::Muted);
	statusLabel->setVisible(!state.statusText.isEmpty());

	lcdWindow->setSegments(state.readout.join(QStringLiteral("  ")));
	lcdWindow->setVisible(!state.readout.isEmpty());

	// The lamp reads the reference's health: green = resolved, amber =
	// service warning, red = broken/unreadable, dark dome = empty slot.
	const SkinTokens& tokens = skinTokens;
	QColor lampColor(tokens.accent2);
	bool lampLit = true;
	if (state.missing)
	{
		if (emptyRef)
			lampLit = false;
		else
			lampColor = QColor(tokens.danger);
	}
	else if (state.statusSeverity == ReferenceCardState::Severity::Critical)
	{
		lampColor = QColor(tokens.danger);
	}
	else if (state.statusSeverity == ReferenceCardState::Severity::Warning)
	{
		lampColor = QColor(tokens.warning);
	}
	lamp->setLamp(lampColor, lampLit);

	// While the reference is broken, the Browse cap carries the engraved
	// LOCATE service lettering (hardware printing, untranslated; the host's
	// translated tooltip stays). Healthy caps go back to the icon.
	if (QAbstractButton* browse = actionButton(ActionRole::Browse))
	{
		const bool locate = locateMode();
		browse->setText(locate ? QStringLiteral("LOCATE") : QString());
		if (QToolButton* toolButton = qobject_cast<QToolButton*>(browse))
			toolButton->setToolButtonStyle(locate ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
		if (browse->property("rackLocate").toBool() != locate)
		{
			browse->setProperty("rackLocate", locate);
			browse->style()->unpolish(browse);
			browse->style()->polish(browse);
		}
	}
}
