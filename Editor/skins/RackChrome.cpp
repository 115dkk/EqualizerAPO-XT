/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	QPainter chrome for the "rack" skin. Constitution: docs/skins/rack.md.
	The tiebreaker for every stroke in
	this file is "would a hardware faceplate have it?" - screws, machined
	grooves, LEDs and engraved printing yes; glows, value arcs and abstract
	decoration no (the only exceptions are the thin keyboard-focus ring and
	the hover highlight, which are UI necessities kept deliberately small).
*/

#include "RackChrome.h"

#include <QAction>
#include <QCheckBox>
#include <QEvent>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QToolBar>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "ISkin.h"
#include "SkinChromeOverlay.h"
#include "SkinPaint.h"

namespace
{
const int kEarWidth = 20;
const qreal kNameplateWidth = 78.0;
const qreal kNameplateHeight = 22.0;

// is-dark / withAlpha live in the shared SkinPaint.h.

// Engraved faceplate printing: a contrast pass offset one pixel down (the
// recess edge catching the light), then the body color on top.
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

// A slotted machine screw: radial-gradient steel body and a slot whose angle
// varies per screw so four of them never read as a stamped texture.
void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 2.1);
	if (dark)
	{
		body.setColorAt(0.0, QColor(0x9A, 0xA4, 0xAC));
		body.setColorAt(0.55, QColor(0x4E, 0x57, 0x5E));
		body.setColorAt(1.0, QColor(0x23, 0x28, 0x2C));
	}
	else
	{
		body.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		body.setColorAt(0.55, QColor(0xC4, 0xBD, 0xAE));
		body.setColorAt(1.0, QColor(0x8E, 0x86, 0x76));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x6B, 0x62, 0x52), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.2);
	const QPointF b = center + dir * (radius - 1.2);
	painter.setPen(QPen(dark ? QColor(10, 12, 14, 230) : QColor(60, 54, 44, 220), 1.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

// A panel LED in a bezel ring: lit = glowing dome with a specular dot,
// unlit = the same dome gone dark.
void paintLed(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(70, 62, 50, 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (lit)
	{
		QRadialGradient halo(center, radius * 3.2);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, radius * 3.2, radius * 3.2);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	if (lit)
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
	painter.setBrush(QColor(255, 255, 255, lit ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

// Horizontal brushing grain: fine strokes whose ink varies
// deterministically per line, so the metal reads as brushed rather than
// evenly striped, with sparse brighter polish lines where the abrasive bit
// deeper. Logical coordinates only - the painter's DPI transform scales the
// grain, no physical-pixel constants.
void paintBrushing(QPainter& painter, const QRectF& r, bool dark, uint seed)
{
	const int baseAlpha = dark ? 4 : 5;
	QColor ink = dark ? QColor(255, 255, 255) : QColor(96, 84, 64);
	for (qreal y = r.top() + 2; y < r.bottom() - 1; y += 2)
	{
		const uint h = (seed ^ uint(qRound(y * 7.0))) * 2654435761u;
		const bool polish = (h >> 8) % 11u == 0;
		ink.setAlpha(baseAlpha + int(h % 7u) + (polish ? 6 : 0));
		painter.setPen(QPen(ink, 1));
		painter.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));
	}
}

// A 1/4" patchbay insert jack: steel flange around a dark sleeve hole.
void paintJack(QPainter& painter, const QPointF& center, bool dark)
{
	QRadialGradient flange(center - QPointF(1.4, 1.4), 7.5);
	if (dark)
	{
		flange.setColorAt(0.0, QColor(0xA8, 0xB1, 0xB8));
		flange.setColorAt(0.6, QColor(0x55, 0x5E, 0x64));
		flange.setColorAt(1.0, QColor(0x26, 0x2B, 0x2F));
	}
	else
	{
		flange.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		flange.setColorAt(0.6, QColor(0xC0, 0xB9, 0xAA));
		flange.setColorAt(1.0, QColor(0x86, 0x7E, 0x6E));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 210) : QColor(0x60, 0x58, 0x48), 1));
	painter.setBrush(flange);
	painter.drawEllipse(center, 4.6, 4.6);

	painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
	painter.setBrush(QColor(8, 9, 10));
	painter.drawEllipse(center, 2.1, 2.1);

	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, dark ? 70 : 150));
	painter.drawEllipse(center + QPointF(-2.5, -2.7), 0.9, 0.9);
}

// Short engraved unit designation for the left rack ear.
QString unitLabel(const CommandRowInfo& info)
{
	static const struct { const char* type; const char* label; } table[] = {
		{ "biquad", "FILTER" },
		{ "graphiceq", "GRAPHIC" },
		{ "include", "PATCH" },
		{ "vst", "VST" },
		{ "copy", "ROUTE" },
		{ "preamp", "PREAMP" },
		{ "channel", "CHANNEL" },
		{ "device", "DEVICE" },
		{ "stage", "STAGE" },
		{ "delay", "DELAY" },
		{ "convolution", "CONV" },
		{ "loudness", "LOUDNESS" },
		{ "comment", "NOTE" },
		{ "text", "AUX" }
	};
	for (const auto& entry : table)
		if (info.type == QLatin1String(entry.type))
			return QLatin1String(entry.label);
	return info.command.toUpper().left(8);
}

// ── Master-rail toolbar chrome ──────────────────────────────────────────────

const char* const kToolbarPlateName = "RackToolbarPlate";
const char* const kToolbarEarSpacerName = "RackToolbarEarSpacer";
// Width of the rail-ear zone at both toolbar ends. QToolBar ignores
// stylesheet padding and QToolBarLayout cannot carry asymmetric margins, so
// the zones are reserved by two fixed-width spacer widgets at the ends of
// the action train; the painter reads their live geometry back, so an ear
// that no longer fits (toolbar overflow) simply is not painted.
const int kRailEarWidth = 24;

// Painted master-rail decoration: everything the QSS base rail cannot
// express. Drawn by the RackToolbarPlate overlay between the toolbar's
// stylesheet background and its controls, in the same faceplate grammar as
// the card chrome so the rail reads as the rack's master section.
void paintToolbarRail(QPainter& painter, const QRect& rect, const QToolBar* toolBar, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect);

	// Brushed-metal sheen: the rolled top edge falling into shadow.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
		sheen.setColorAt(0.14, QColor(255, 255, 255, 10));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 52));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 120));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 30));
	}
	painter.fillRect(r, sheen);

	// Horizontal brushing grain, same machine as the card faceplates.
	paintBrushing(painter, r, dark, uint(qHash(QStringLiteral("master-rail-brush"))));

	// Machined edges: lit top chamfer, shadowed groove above the QSS border,
	// so the strip reads as a milled rail rather than a flat band.
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
	painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

	// Rail ears with one mounting screw each, painted at the live geometry of
	// the ear spacers (an ear pushed out by toolbar overflow is not painted,
	// so the screw never sits under a control). The slot angles differ -
	// hand-tightened, like the card corners.
	const uint seed = uint(qHash(QStringLiteral("master-rail")));
	const QColor earFill(0, 0, 0, dark ? 52 : 20);
	for (const QWidget* spacer : toolBar->findChildren<QWidget*>(QLatin1String(kToolbarEarSpacerName), Qt::FindDirectChildrenOnly))
	{
		if (!spacer->isVisible() || spacer->width() < kRailEarWidth - 4)
			continue;
		const QRectF g(spacer->geometry());
		const bool leftSide = g.center().x() < r.center().x();
		// The ear runs from the rail edge to the machined groove that
		// separates it from the panel.
		const QRectF ear = leftSide
			? QRectF(r.left(), r.top(), g.right() - r.left(), r.height())
			: QRectF(g.left(), r.top(), r.right() - g.left(), r.height());
		const qreal grooveX = leftSide ? ear.right() : ear.left();
		painter.fillRect(ear, earFill);
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
		painter.drawLine(QPointF(grooveX, r.top()), QPointF(grooveX, r.bottom()));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
		painter.drawLine(QPointF(grooveX + (leftSide ? 1 : -1), r.top()), QPointF(grooveX + (leftSide ? 1 : -1), r.bottom()));
		paintScrew(painter, QPointF(ear.center().x(), r.center().y()), 4.0,
			qreal((seed + (leftSide ? 0u : 73u)) % 180u), dark);
	}

	// Engraved section designation on the blank panel between the save-state
	// readout and the device selectors (the expanding spacer), drawn only
	// when the blank leaves room. Hardware printing, not a UI string -
	// never translated.
	const QString marking = QStringLiteral("MASTER");
	QFont markFont(tokens.fontFamily);
	markFont.setPixelSize(8);
	markFont.setBold(true);
	markFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
	const QFontMetricsF metrics(markFont);
	const QWidget* blank = nullptr;
	for (const QWidget* spacer : toolBar->findChildren<QWidget*>(QStringLiteral("ToolBarSpacer"), Qt::FindDirectChildrenOnly))
		if (blank == nullptr || spacer->width() > blank->width())
			blank = spacer;
	if (blank != nullptr && blank->width() >= metrics.horizontalAdvance(marking) + 16)
	{
		painter.setFont(markFont);
		QColor ink(tokens.mutedText);
		ink.setAlpha(dark ? 150 : 190);
		engraveText(painter, QRectF(blank->geometry()), Qt::AlignCenter, marking, ink, dark);
	}

	// The instant-mode power LED, mounted in the well the checkbox's QSS
	// padding reserves (its indicator is collapsed): lit green while the
	// mode is engaged, a dark dome when it is off.
	if (const QCheckBox* box = toolBar->findChild<QCheckBox*>(QStringLiteral("InstantModeCheckBox"), Qt::FindDirectChildrenOnly))
	{
		const QRectF g(box->geometry());
		paintLed(painter, QPointF(g.left() + 10.0, g.center().y()), 3.2, QColor(tokens.accent2),
			box->isChecked() && box->isEnabled(), dark);
	}
}

// The transparent overlay carrying the painted rail chrome. It sits as the
// bottom-most child of the toolbar (above the QSS background, below the
// controls), tracks the toolbar's size, repaints when the instant-mode
// switch toggles and hides itself when another skin's stylesheet takes over
// (every skin switch delivers StyleChange to the toolbar). No Q_OBJECT: it
// is found again by object name and only connects to existing signals.
class RackToolbarPlate : public SkinChromeOverlay
{
public:
	explicit RackToolbarPlate(QToolBar* parentToolBar)
		: SkinChromeOverlay(parentToolBar, QLatin1String(kToolbarPlateName),
			QStringLiteral("rack"), ZPolicy::BelowControls)
	{
		if (QCheckBox* box = parentToolBar->findChild<QCheckBox*>(QStringLiteral("InstantModeCheckBox"), Qt::FindDirectChildrenOnly))
			connect(box, &QCheckBox::toggled, this, QOverload<>::of(&QWidget::update));
	}

	void setTokens(const SkinTokens& newTokens)
	{
		tokens = newTokens;
	}

	// The actions wrapping the two ear spacers; their visibility follows the
	// plate's, so the reserved zones vanish with the chrome when another
	// skin takes over.
	void setEarActions(QAction* left, QAction* right)
	{
		leftEarAction = left;
		rightEarAction = right;
	}

	// The StyleChange filter below hides the ears when another skin's sheet
	// arrives, but a same-skin re-apply is skipped upstream and delivers no
	// StyleChange - the reuse path calls this so the zones come back with
	// the chrome regardless of how the hook was reached.
	void showEarZones()
	{
		if (leftEarAction != nullptr)
			leftEarAction->setVisible(true);
		if (rightEarAction != nullptr)
			rightEarAction->setVisible(true);
	}

protected:
	void ownerActiveChanged(bool active) override
	{
		if (leftEarAction != nullptr)
			leftEarAction->setVisible(active);
		if (rightEarAction != nullptr)
			rightEarAction->setVisible(active);
	}

	void paintChrome(QPainter& painter) override
	{
		paintToolbarRail(painter, rect(), parentToolBar(), tokens);
	}

private:
	QAction* leftEarAction = nullptr;
	QAction* rightEarAction = nullptr;
	SkinTokens tokens;
};

// A fixed-width blank widget reserving one rail-ear zone in the toolbar's
// action train. The plate paints the ear and its screw at this widget's
// live geometry.
QWidget* makeEarSpacer(QWidget* parent)
{
	QWidget* spacer = new QWidget(parent);
	spacer->setObjectName(QLatin1String(kToolbarEarSpacerName));
	spacer->setFixedWidth(kRailEarWidth);
	spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
	// Reserved space only: without this the universal QWidget background
	// rule stamps an opaque patch over the rail ear the plate paints.
	spacer->setAttribute(Qt::WA_NoSystemBackground, true);
	return spacer;
}
}

namespace RackChrome
{
int earWidth()
{
	return kEarWidth;
}

int nameplateReserve()
{
	return int(kNameplateWidth) + 14;
}

void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens)
{
	// Blank lines are gaps in the rack, not units: no faceplate.
	if (info.type == QLatin1String("spacer"))
		return;

	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing);

	// Stay inside the 1px QSS border (the machined plate edge) and clip all
	// painting to the plate so nothing bleeds past the rounded corners.
	const QRectF r = QRectF(rect).adjusted(1, 1, -1, -1);
	const qreal radius = qMax(2, tokens.borderRadius - 1);
	QPainterPath plate;
	plate.addRoundedRect(r, radius, radius);
	painter.setClipPath(plate);

	// Brushed-metal sheen: a bright rolled top edge falling into shadow.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
		sheen.setColorAt(0.12, QColor(255, 255, 255, 9));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 46));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 110));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 26));
	}
	painter.fillRect(r, sheen);

	// Per-type finish: Include units wear patchbay black, VST units a warm
	// charcoal; filters keep the bare aluminium.
	if (info.type == QLatin1String("include"))
		painter.fillRect(r, QColor(0, 0, 0, dark ? 64 : 28));
	else if (info.type == QLatin1String("vst"))
		painter.fillRect(r, dark ? QColor(34, 20, 6, 50) : QColor(74, 50, 14, 18));

	// Horizontal brushing grain, seeded per unit so two stacked units never
	// share the same streak pattern - sheets cut from the same stock.
	paintBrushing(painter, r, dark, uint(qHash(info.command)));

	// Rack ears, separated from the panel by a machined groove.
	const QRectF leftEar(r.left(), r.top(), kEarWidth, r.height());
	const QRectF rightEar(r.right() - kEarWidth, r.top(), kEarWidth, r.height());
	const QColor earFill(0, 0, 0, dark ? 52 : 20);
	painter.fillRect(leftEar, earFill);
	painter.fillRect(rightEar, earFill);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
	painter.drawLine(QPointF(leftEar.right(), r.top()), QPointF(leftEar.right(), r.bottom()));
	painter.drawLine(QPointF(rightEar.left(), r.top()), QPointF(rightEar.left(), r.bottom()));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
	painter.drawLine(QPointF(leftEar.right() + 1, r.top()), QPointF(leftEar.right() + 1, r.bottom()));
	painter.drawLine(QPointF(rightEar.left() + 1, r.top()), QPointF(rightEar.left() + 1, r.bottom()));

	// Unit seating and bezel: a dark seam runs just inside the QSS
	// border (the plate sitting in its rack opening), then the chamfer obeys
	// the one work light - lit along the top and left, falling into shadow
	// along the bottom and right - so every row reads as its own bolted
	// unit rather than a list stripe.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 90 : 40), 1));
	painter.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left() + radius, r.top() + 1.5), QPointF(r.right() - radius, r.top() + 1.5));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 16 : 80), 1));
	painter.drawLine(QPointF(r.left() + 1.5, r.top() + radius), QPointF(r.left() + 1.5, r.bottom() - radius));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 140 : 70), 1));
	painter.drawLine(QPointF(r.left() + radius, r.bottom() - 1.5), QPointF(r.right() - radius, r.bottom() - 1.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 60 : 30), 1));
	painter.drawLine(QPointF(r.right() - 1.5, r.top() + radius), QPointF(r.right() - 1.5, r.bottom() - radius));

	// Module groove under the control strip whenever the unit is opened (the
	// body below it then reads as controls mounted on the same module).
	if (r.height() >= tokens.rowHeight + 26)
	{
		const qreal y = r.top() + tokens.rowHeight;
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 110 : 55), 1));
		painter.drawLine(QPointF(leftEar.right() + 2, y), QPointF(rightEar.left() - 2, y));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 24 : 110), 1));
		painter.drawLine(QPointF(leftEar.right() + 2, y + 1), QPointF(rightEar.left() - 2, y + 1));
	}

	// Four corner screws (two on very low rows).
	const uint seed = uint(qHash(info.command));
	const QPointF screws[] = {
		QPointF(r.left() + 10, r.top() + 9),
		QPointF(r.right() - 10, r.top() + 9),
		QPointF(r.left() + 10, r.bottom() - 9),
		QPointF(r.right() - 10, r.bottom() - 9)
	};
	const int screwCount = r.height() >= 40 ? 4 : 2;
	for (int i = 0; i < screwCount; i++)
		paintScrew(painter, screws[i], 4.0, qreal((seed + uint(i) * 73u) % 180u), dark);

	// Status LEDs on the left ear: green power LED (lit = line active), amber
	// SELECT LED below it.
	paintLed(painter, QPointF(r.left() + 10, r.top() + 21), 3.0, QColor(tokens.accent2), info.enabled, dark);
	if (r.height() >= 44)
		paintLed(painter, QPointF(r.left() + 10, r.top() + 31.5), 2.4, QColor(tokens.accent), info.selected, dark);

	// Include: patchbay insert jacks on the right ear.
	if (info.type == QLatin1String("include"))
	{
		paintJack(painter, QPointF(r.right() - 10, r.top() + 21), dark);
		if (r.height() >= 46)
			paintJack(painter, QPointF(r.right() - 10, r.top() + 32.5), dark);
	}

	// VST: riveted brass brand nameplate right of the control strip. The
	// header layout reserves this area (see RackSkin::prepareCommandRow).
	if (info.type == QLatin1String("vst") && r.width() >= 320)
	{
		const QRectF plateRect(rightEar.left() - 8 - kNameplateWidth,
			r.top() + (tokens.rowHeight - kNameplateHeight) / 2.0, kNameplateWidth, kNameplateHeight);
		QLinearGradient brass(plateRect.topLeft(), plateRect.bottomLeft());
		if (dark)
		{
			brass.setColorAt(0.0, QColor(0xD6, 0xB2, 0x6A));
			brass.setColorAt(0.5, QColor(0xA8, 0x85, 0x46));
			brass.setColorAt(1.0, QColor(0x86, 0x67, 0x30));
		}
		else
		{
			brass.setColorAt(0.0, QColor(0xE8, 0xC8, 0x86));
			brass.setColorAt(0.5, QColor(0xC4, 0xA0, 0x5C));
			brass.setColorAt(1.0, QColor(0x9A, 0x7A, 0x3C));
		}
		painter.setPen(QPen(QColor(0x5A, 0x44, 0x16), 1));
		painter.setBrush(brass);
		painter.drawRoundedRect(plateRect, 3, 3);

		QFont plateFont(tokens.fontFamily);
		plateFont.setPixelSize(9);
		plateFont.setBold(true);
		plateFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
		painter.setFont(plateFont);
		// Engraved into the brass itself, so the passes are brass-tinted in
		// both modes rather than following the panel's engraving direction.
		painter.setPen(QColor(255, 240, 200, 160));
		painter.drawText(plateRect.translated(1, 1), Qt::AlignCenter, QStringLiteral("VST"));
		painter.setPen(QColor(0x3A, 0x2A, 0x0C));
		painter.drawText(plateRect, Qt::AlignCenter, QStringLiteral("VST"));

		painter.setPen(QPen(QColor(0x55, 0x40, 0x14), 0.8));
		painter.setBrush(QColor(0xE9, 0xD3, 0x9A));
		painter.drawEllipse(QPointF(plateRect.left() + 6, plateRect.center().y()), 1.6, 1.6);
		painter.drawEllipse(QPointF(plateRect.right() - 6, plateRect.center().y()), 1.6, 1.6);
	}

	// Engraved unit designation running up the left ear on tall units.
	const QString label = unitLabel(info);
	if (!label.isEmpty() && r.height() >= 96)
	{
		QFont earFont(tokens.fontFamily);
		earFont.setPixelSize(8);
		earFont.setBold(true);
		earFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		QPainterStateGuard labelState(&painter);
		painter.translate(r.left() + 14.5, r.bottom() - 16);
		painter.rotate(-90);
		painter.setFont(earFont);
		const QRectF textRect(0, -10, r.height() - 64, 20);
		engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, label, withAlpha(QColor(tokens.mutedText), 200), dark);
	}

	// Commented-out line: the whole unit is powered down behind a dim film.
	if (!info.enabled)
		painter.fillPath(plate, dark ? QColor(0, 0, 0, 80) : QColor(255, 252, 244, 120));

	// Keyboard focus: a thin amber line along the inner bezel (UI necessity,
	// kept as small as a hardware unit's rail light).
	if (info.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 190), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
	}
}

void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);

	// Work inside a centred square (promoted legacy dials are 100x66).
	const QRectF inner = QRectF(rect).adjusted(4, 4, -4, -4);
	const qreal side = qMin(inner.width(), inner.height());
	const QPointF center = inner.center();
	const qreal scaleRadius = side / 2.0;        // printed panel scale
	const qreal bodyRadius = scaleRadius - 9.0;  // physical knob body

	// Value geometry matches AudioKnob's input mapping: minimum at 135
	// degrees (bottom-left), 270-degree clockwise sweep, maximum at the
	// bottom-right, dead zone across the bottom.
	auto pointAt = [center](qreal ratio, qreal radius) {
		const qreal a = qDegreesToRadians(135.0 + 270.0 * ratio);
		return center + QPointF(qCos(a) * radius, qSin(a) * radius);
	};

	// Scale ticks are printed on the PANEL around the knob, never on the
	// knob - they do not move and there is no value arc; the pointer alone
	// carries the value, as on real hardware. The printing must read at
	// a glance, so the end stops are majors in full panel ink while the
	// intermediates stay muted; the bipolar neutral (0 dB at 12 o'clock) is
	// the boldest mark on the plate - the centre detent, in amber, longer
	// and heavier than any other tick.
	const QColor inkBase(tokens.mutedText);
	const QColor inkStrong(tokens.text);
	const int inkAlpha = state.enabled ? 235 : 90;
	const int minorAlpha = state.enabled ? 165 : 70;
	for (int i = 0; i <= 10; i++)
	{
		const qreal ratio = i / 10.0;
		const bool centerTick = state.bipolar && i == 5;
		const bool major = i == 0 || i == 10 || centerTick;
		QColor ink = centerTick ? QColor(tokens.accent) : (major ? inkStrong : inkBase);
		ink.setAlpha(major ? inkAlpha : minorAlpha);
		const qreal innerRadius = centerTick ? bodyRadius + 2.0 : bodyRadius + 3.5;
		const qreal outerRadius = centerTick ? scaleRadius + 2.0 : (major ? scaleRadius + 0.5 : scaleRadius - 1.5);
		painter.setPen(QPen(ink, centerTick ? 3.0 : (major ? 2.0 : 1.0), Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(pointAt(ratio, innerRadius), pointAt(ratio, outerRadius));
	}

	// Bipolar knobs print the cut/boost glyphs in the dead zone under the
	// scale ends, like a gain pot's faceplate. Unipolar knobs stay plain, so
	// the two kinds never look alike. The glyphs are engraved (contrast
	// pass offset one pixel down) in full panel ink, large enough to read at
	// the gallery's knob size.
	if (state.bipolar)
	{
		QFont glyphFont(tokens.fontFamily);
		glyphFont.setPixelSize(11);
		glyphFont.setBold(true);
		painter.setFont(glyphFont);
		const QPointF minusAt = pointAt(-0.07, scaleRadius - 2.5);
		const QPointF plusAt = pointAt(1.07, scaleRadius - 2.5);
		const QRectF minusRect(minusAt.x() - 7, minusAt.y() - 7, 14, 14);
		const QRectF plusRect(plusAt.x() - 7, plusAt.y() - 7, 14, 14);
		painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
		painter.drawText(minusRect.translated(0, 1), Qt::AlignCenter, QStringLiteral("-"));
		painter.drawText(plusRect.translated(0, 1), Qt::AlignCenter, QStringLiteral("+"));
		painter.setPen(withAlpha(inkStrong, inkAlpha));
		painter.drawText(minusRect, Qt::AlignCenter, QStringLiteral("-"));
		painter.drawText(plusRect, Qt::AlignCenter, QStringLiteral("+"));
	}

	// Knob body: bakelite (dark mode) / machined aluminium (light mode) with
	// an offset highlight suggesting the work light.
	QRadialGradient bodyGrad(center - QPointF(bodyRadius * 0.4, bodyRadius * 0.4), bodyRadius * 2.2);
	const QColor cap(tokens.card);
	if (dark)
	{
		bodyGrad.setColorAt(0.0, cap.lighter(190));
		bodyGrad.setColorAt(0.6, cap.lighter(115));
		bodyGrad.setColorAt(1.0, cap.darker(160));
	}
	else
	{
		bodyGrad.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFF));
		bodyGrad.setColorAt(0.6, QColor(0xDE, 0xD7, 0xC6));
		bodyGrad.setColorAt(1.0, QColor(0xA8, 0x9F, 0x8C));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x7E, 0x75, 0x62), 1));
	painter.setBrush(bodyGrad);
	painter.drawEllipse(center, bodyRadius, bodyRadius);

	// Machined cap step and the specular arc on its top edge.
	const qreal capRadius = bodyRadius - 3.5;
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 90 : 50), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, capRadius, capRadius);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 70 : 150), 1.2));
	painter.drawArc(QRectF(center.x() - capRadius, center.y() - capRadius, capRadius * 2, capRadius * 2), 60 * 16, 60 * 16);

	// The pointer: a physical painted line. Hover/drag turns it amber (the
	// hand is on the knob), disabled grays it out. A recessed shadow
	// pass underneath and a tip reaching the knob skirt keep the pointer
	// readable against the printed scale at any angle.
	QColor pointerColor;
	if (!state.enabled)
		pointerColor = withAlpha(inkBase, 130);
	else if (state.dragging || state.hovered)
		pointerColor = QColor(tokens.accent);
	else
		pointerColor = dark ? QColor(0xF2, 0xEC, 0xDC) : QColor(0x2E, 0x29, 0x22);
	const QPointF pointerBase = pointAt(state.ratio, bodyRadius * 0.28);
	const QPointF pointerTip = pointAt(state.ratio, bodyRadius - 1.8);
	painter.setPen(QPen(QColor(0, 0, 0, state.enabled ? (dark ? 150 : 90) : 50), 3.6, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);
	painter.setPen(QPen(pointerColor, 2.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);

	// Hover/drag: the knob rim catches the light.
	if (state.enabled && (state.hovered || state.dragging))
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), 90), 1.4));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, bodyRadius + 0.8, bodyRadius + 0.8);
	}

	// Keyboard focus: thin ring around the printed scale.
	if (state.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 180), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, scaleRadius + 2.0, scaleRadius + 2.0);
	}

	// Powered-down knob: dim film over the body.
	if (!state.enabled)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(dark ? QColor(0, 0, 0, 90) : QColor(255, 252, 244, 130));
		painter.drawEllipse(center, bodyRadius, bodyRadius);
	}

	// No value window on the knob itself: a display pane across the cap is
	// not buildable hardware and it would cut the pointer line in half.
	// The value lives in the card's own LED
	// display (EditableValue) beside the knob; state.valueText is
	// deliberately unused here.
}

void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMax(2, tokens.borderRadius - 1);
	QPainterPath opening;
	opening.addRoundedRect(r, radius, radius);
	painter.setClipPath(opening);

	// The bay's blank panel is missing, so the opening shows the rack's
	// interior - dark in both modes (the inside of a rack has no finish).
	QLinearGradient interior(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		interior.setColorAt(0.0, QColor(0x03, 0x04, 0x05));
		interior.setColorAt(0.4, QColor(0x0A, 0x0C, 0x0E));
		interior.setColorAt(1.0, QColor(0x12, 0x15, 0x18));
	}
	else
	{
		interior.setColorAt(0.0, QColor(0x2E, 0x2A, 0x23));
		interior.setColorAt(0.4, QColor(0x42, 0x3D, 0x33));
		interior.setColorAt(1.0, QColor(0x52, 0x4B, 0x3F));
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(interior);
	painter.drawRoundedRect(r, radius, radius);

	// The rack's mounting rails run behind the ear zones, each with two
	// empty bolt holes waiting for a unit's ears. The rails are the frame's
	// steel, one step lighter than the interior darkness.
	const QRectF leftRail(r.left(), r.top(), kEarWidth, r.height());
	const QRectF rightRail(r.right() - kEarWidth, r.top(), kEarWidth, r.height());
	const QColor railFill(255, 255, 255, dark ? 14 : 24);
	painter.fillRect(leftRail, railFill);
	painter.fillRect(rightRail, railFill);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 130), 1));
	painter.drawLine(QPointF(leftRail.right(), r.top()), QPointF(leftRail.right(), r.bottom()));
	painter.drawLine(QPointF(rightRail.left(), r.top()), QPointF(rightRail.left(), r.bottom()));

	auto paintBoltHole = [&painter, dark](const QPointF& center) {
		// An empty threaded hole: a dark bore whose lower rim catches the
		// work light - recessed, so the light law is the plate chamfer's
		// inverse.
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, dark ? 210 : 180));
		painter.drawEllipse(center, 2.6, 2.6);
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 40 : 70), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawArc(QRectF(center.x() - 2.6, center.y() - 2.6, 5.2, 5.2), 200 * 16, 140 * 16);
	};
	const qreal holeOffset = qMin(9.0, r.height() / 2.0 - 5.0);
	for (const QRectF& rail : { leftRail, rightRail })
	{
		paintBoltHole(QPointF(rail.center().x(), r.center().y() - holeOffset));
		paintBoltHole(QPointF(rail.center().x(), r.center().y() + holeOffset));
	}

	// The opening's chamfer is the faceplate's inverse: the top inner edge
	// falls into the overhang's shadow, the lower lip catches the work
	// light.
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 170 : 150), 1));
	painter.drawLine(QPointF(r.left() + radius, r.top() + 1.0), QPointF(r.right() - radius, r.top() + 1.0));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 50), 1));
	painter.drawLine(QPointF(r.left() + radius, r.bottom() - 1.0), QPointF(r.right() - radius, r.bottom() - 1.0));

	// Stencilled marking inside the bay - hardware printing, never
	// translated (the tooltip carries the accessible caption). Always the
	// dark-recess engraving pass: the interior is dark in both finishes.
	const bool warm = state.hovered || state.pressed;
	QFont stencilFont(tokens.fontFamily);
	stencilFont.setPixelSize(9);
	stencilFont.setBold(true);
	stencilFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
	painter.setFont(stencilFont);
	QColor stencilInk;
	if (warm)
		stencilInk = withAlpha(QColor(tokens.accent), state.pressed ? 255 : 225);
	else
		stencilInk = dark ? QColor(0x8A, 0x84, 0x78, 170) : QColor(0xB8, 0xAF, 0x9E, 190);
	const QRectF stencilRect = r.adjusted(kEarWidth + 6, 0, -kEarWidth - 6, 0);
	engraveText(painter, stencilRect, Qt::AlignCenter,
		warm ? QStringLiteral("INSTALL MODULE") : QStringLiteral("EMPTY BAY"), stencilInk, true);

	// Hover pre-heat: the bay's bezel warms amber, brightening under the
	// pressed finger - a lamp answer, not a button lift.
	if (warm)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), state.pressed ? 190 : 120), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r, radius, radius);
	}

	// Keyboard focus: the thin service ring just inside the opening.
	if (state.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 190), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
	}
}

void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens)
{
	// At rest the seam does not exist - the widget only calls this while
	// hovered, but keep the contract locally honest too.
	if (!state.hovered && !state.pressed)
		return;

	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing);

	// A service slot heating up between the rail and the first unit:
	// strokes only. The machined groove first (a dark shadow line), then
	// the amber heat line over it, then the slot ticks marking where the
	// ears of the incoming unit will sit.
	const qreal y = rect.center().y();
	const qreal left = rect.left();
	const qreal right = rect.right();
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 90), 1));
	painter.drawLine(QPointF(left, y + 1.5), QPointF(right, y + 1.5));

	QColor amber(tokens.accent);
	// A wide faint pass under the line suggests the heat bleeding into the
	// metal (still a stroke - no fills, no discs).
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 70 : 45), 4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left + kEarWidth, y), QPointF(right - kEarWidth, y));
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 255 : 210), state.pressed ? 2.0 : 1.4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left, y), QPointF(right, y));

	// Ear ticks: short strokes at the ear grooves' positions.
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 255 : 210), 1.4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left + kEarWidth, y - 3), QPointF(left + kEarWidth, y + 3));
	painter.drawLine(QPointF(right - kEarWidth, y - 3), QPointF(right - kEarWidth, y + 3));
}

void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	const bool powered = state.enabled;
	QPainterStateGuard painterState(&painter);

	// The scope well is dark in BOTH finishes (the display law). The
	// graticule sits in the scope-grid family: the cream table's grid token
	// is panel paint, so it never reaches the glass.
	const QColor glassTop = dark ? QColor(0x04, 0x06, 0x05) : QColor(0x0A, 0x0E, 0x0B);
	const QColor glassBottom = dark ? QColor(0x0A, 0x0F, 0x0C) : QColor(0x11, 0x16, 0x10);
	const QColor bezel = dark ? QColor(0x05, 0x08, 0x07) : QColor(0x4A, 0x44, 0x38);
	const QColor bezelLip = dark ? QColor(0x39, 0x42, 0x4A) : QColor(0x6B, 0x63, 0x54);
	const QColor gridMinor = dark ? QColor(tokens.graphGridMinor) : QColor(0x25, 0x43, 0x37);
	const QColor gridMajor = gridMinor.lighter(168);
	// Phosphor: accent2 is the machine's LED green. The cream panel's token
	// is paint, not light, so it is lifted to emission strength on the glass.
	const QColor phosphor = dark ? QColor(tokens.accent2) : QColor(tokens.accent2).lighter(195);
	// Etched axis figures and the cursor readout follow the sheets' LCD
	// segment palette; a powered-down display dims to the service shade.
	const QColor segmentBright = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
	const QColor segmentDim = dark ? QColor(0x4C, 0x9E, 0x74) : QColor(0x2F, 0x8A, 0x61);
	const QColor segmentOff = dark ? QColor(0x3A, 0x6B, 0x51) : QColor(0x2F, 0x6B, 0x4D);

	const QRectF r = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = 2.0;
	QPainterPath glass;
	glass.addRoundedRect(r, radius, radius);
	painter.setClipPath(glass);

	// Glass ground: the tube face, slightly deeper at the top under the
	// bezel's overhang.
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient ground(r.topLeft(), r.bottomLeft());
	ground.setColorAt(0.0, glassTop);
	ground.setColorAt(1.0, glassBottom);
	painter.fillRect(r, ground);

	// While powered, the faint memory of the beam warms the middle of the
	// tube (a fill, not an effect).
	if (powered)
	{
		QRadialGradient backGlow(state.plotRect.center(), state.plotRect.width() * 0.55);
		backGlow.setColorAt(0.0, withAlpha(phosphor, dark ? 12 : 14));
		backGlow.setColorAt(1.0, withAlpha(phosphor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(backGlow);
		painter.drawRect(state.plotRect);
	}

	// Graticule: crisp 1px rules - straight lines carry no antialiasing.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const int gridAlpha = powered ? 255 : 150;
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());
	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		painter.setPen(QPen(withAlpha(line.major ? gridMajor : gridMinor, gridAlpha), 1));
		painter.drawLine(int(line.pos), plotTop, int(line.pos), plotBottom);
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		painter.setPen(QPen(withAlpha(line.major ? gridMajor : gridMinor, gridAlpha), 1));
		painter.drawLine(plotLeft, int(line.pos), plotRight, int(line.pos));
	}

	// The 0 dB centre axis: a phosphor-tinted rule with the scope's fine
	// hash marks between the graticule columns.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		const int zeroY = int(state.zeroY);
		painter.setPen(QPen(withAlpha(powered ? phosphor : segmentOff, powered ? 145 : 80), 1));
		painter.drawLine(plotLeft, zeroY, plotRight, zeroY);
		painter.setPen(QPen(withAlpha(powered ? phosphor : segmentOff, powered ? 60 : 40), 1));
		for (int x = plotLeft + 4; x < plotRight - 2; x += 7)
			painter.drawLine(x, zeroY - 2, x, zeroY + 2);
	}

	// Axis figures: etched in the glass margins in segment ink (numerals,
	// never translated). Majors read a step brighter than minors.
	QFont axisFont(tokens.monoFontFamily);
	axisFont.setPointSizeF(7.0);
	axisFont.setBold(true);
	painter.setFont(axisFont);
	const QColor axisInk = powered ? segmentDim : segmentOff;
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(axisInk, line.major ? (powered ? 235 : 170) : (powered ? 140 : 100)));
		painter.drawText(QRect(int(line.pos) - 24, plotBottom + 2, 48, state.rect.bottom() - plotBottom - 2),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(axisInk, line.major ? (powered ? 235 : 170) : (powered ? 140 : 100)));
		painter.drawText(QRect(state.rect.left() + 2, int(line.pos) - 8, plotLeft - state.rect.left() - 6, 16),
			Qt::AlignRight | Qt::AlignVCenter, line.label);
	}

	// The beam stays inside the graticule area, like a tube masks its trace.
	QPainterStateGuard beamState(&painter);
	painter.setClipRect(state.plotRect.adjusted(-1, -1, 1, 1), Qt::IntersectClip);

	if (state.curve.size() >= 2)
	{
		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());

		// Afterglow: a faint phosphor wash between the trace and the axis.
		QPolygonF afterglow = state.curve;
		afterglow.append(QPointF(state.curve.last().x(), base));
		afterglow.prepend(QPointF(state.curve.first().x(), base));
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(phosphor, powered ? 22 : 10));
		painter.drawPolygon(afterglow);

		// The trace: glow faked by stroke overpainting (wide dim passes under
		// the beam core - no graphics effects on this machine). A powered-down
		// display keeps a single dim burned-in trace.
		painter.setBrush(Qt::NoBrush);
		if (powered)
		{
			painter.setPen(QPen(withAlpha(phosphor, 26), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
			painter.setPen(QPen(withAlpha(phosphor, 70), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
			painter.setPen(QPen(phosphor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
		}
		else
		{
			painter.setPen(QPen(withAlpha(segmentOff, 200), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
		}

		// Band-locked layouts (15/31) read as levels on fixed bands: each
		// node hangs a segmented level ladder off the centre axis, the way a
		// hardware analyzer steps its columns.
		if (state.bandLocked)
		{
			painter.setRenderHint(QPainter::Antialiasing, false);
			painter.setPen(QPen(powered ? withAlpha(phosphor, 110) : withAlpha(segmentOff, 80), 3));
			for (const QPointF& node : state.nodePositions)
			{
				const qreal x = qFloor(node.x()) + 0.5;
				const qreal length = qAbs(node.y() - base);
				const qreal direction = node.y() < base ? -1.0 : 1.0;
				for (qreal offset = 2.0; offset + 3.0 <= length; offset += 5.0)
					painter.drawLine(QPointF(x, base + direction * offset), QPointF(x, base + direction * (offset + 3.0)));
			}
		}
	}

	// Nodes are glowing adjustment dots: rest = a quiet dome, hover = the
	// dome pre-heats, selected = lit core with the adjustment collar ring.
	painter.setRenderHint(QPainter::Antialiasing, true);
	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool warmed = state.hoveredNode == i;

		if (!powered)
		{
			// Power off: the adjustment points survive as dark domes.
			painter.setPen(QPen(withAlpha(segmentOff, 150), 1));
			painter.setBrush(withAlpha(segmentOff, 60));
			painter.drawEllipse(center, 2.6, 2.6);
			continue;
		}

		const qreal haloRadius = selected ? 9.0 : (warmed ? 8.0 : 5.5);
		QRadialGradient halo(center, haloRadius);
		halo.setColorAt(0.0, withAlpha(phosphor, selected ? 110 : (warmed ? 85 : 45)));
		halo.setColorAt(1.0, withAlpha(phosphor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, haloRadius, haloRadius);

		const QColor core = selected ? phosphor.lighter(145) : (warmed ? phosphor.lighter(118) : phosphor);
		painter.setBrush(withAlpha(core, selected ? 255 : (warmed ? 240 : 205)));
		painter.drawEllipse(center, selected ? 3.2 : 2.7, selected ? 3.2 : 2.7);

		if (selected)
		{
			painter.setPen(QPen(withAlpha(phosphor, 220), 1.2));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, 5.4, 5.4);
		}
		// The keyboard target wears the amber service ring while the display
		// holds focus - the machine's focus law reaching into the glass.
		if (state.focused && state.focusedNode == i)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 200), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, 7.0, 7.0);
		}
	}
	beamState.restore();

	// Cursor readout: top-right inside the glass, bright segments.
	if (powered && state.cursorValid && !state.cursorText.isEmpty())
	{
		QFont readoutFont(tokens.monoFontFamily);
		readoutFont.setPointSizeF(7.5);
		readoutFont.setBold(true);
		painter.setFont(readoutFont);
		painter.setPen(segmentBright);
		painter.drawText(QRectF(state.plotRect.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The bezel's overhang shadow hangs over the top of the glass - the
	// recessed grammar (shadowed top edge, lit lower lip below).
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient overhang(r.topLeft(), QPointF(r.left(), r.top() + 9.0));
	overhang.setColorAt(0.0, QColor(0, 0, 0, dark ? 150 : 130));
	overhang.setColorAt(1.0, QColor(0, 0, 0, 0));
	painter.fillRect(QRectF(r.left(), r.top(), r.width(), 9.0), overhang);

	// Bezel frame: the LCD-well border grammar. Focus lights the amber
	// service edge; at rest the bottom border is the lit lower lip.
	painter.setClipping(false);
	painter.setBrush(Qt::NoBrush);
	if (state.focused)
	{
		painter.setPen(QPen(QColor(tokens.focusRing), 1));
		painter.drawRoundedRect(r, radius, radius);
	}
	else
	{
		painter.setPen(QPen(bezel, 1));
		painter.drawRoundedRect(r, radius, radius);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(bezelLip, 1));
		painter.drawLine(QPointF(r.left() + radius, r.bottom()), QPointF(r.right() - radius, r.bottom()));
	}
}

void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect);

	// Brushed-metal sheen: the rolled top edge falling into shadow, the same
	// finish as the card faceplates and the master rail - one machine.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
		sheen.setColorAt(0.14, QColor(255, 255, 255, 10));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 52));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 120));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 30));
	}
	painter.fillRect(r, sheen);

	// Horizontal brushing grain, same machine as the card faceplates.
	paintBrushing(painter, r, dark, uint(qHash(QStringLiteral("top-panel-brush"))));

	// The caption-button block is the panel's right ear: a slightly recessed
	// zone behind the three machined caps, set off by a machined groove. The
	// caps are fixed-size (TitleBar::makeCaptionButton), so the groove sits at
	// the same scaled offset the layout gives them.
	const qreal capsWidth = qreal(GUIHelper::scale(40.0)) * 3.0;
	const qreal grooveX = r.right() - capsWidth - 6.0;
	const bool earFits = grooveX > r.left() + 120.0;
	if (earFits)
	{
		painter.fillRect(QRectF(grooveX, r.top(), r.right() - grooveX, r.height()),
			QColor(0, 0, 0, dark ? 52 : 20));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
		painter.drawLine(QPointF(grooveX, r.top()), QPointF(grooveX, r.bottom()));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
		painter.drawLine(QPointF(grooveX + 1, r.top()), QPointF(grooveX + 1, r.bottom()));
	}

	// Machined edges across the full rail (over the ear fill): lit top
	// chamfer, shadowed bottom groove against the menu bar below.
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
	painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

	// Two rail screws bolting the top panel down: one at the left end before
	// the engraved designation, one on the blank panel before the caption
	// ear. Slot angles differ - hand-tightened, like everywhere else.
	const uint seed = uint(qHash(QStringLiteral("top-panel")));
	paintScrew(painter, QPointF(r.left() + 10.0, r.center().y()), 4.0, qreal(seed % 180u), dark);
	if (earFits)
		paintScrew(painter, QPointF(grooveX - 12.0, r.center().y()), 4.0, qreal((seed + 73u) % 180u), dark);
}

void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens)
{
	if (toolBar == nullptr)
		return;

	// Hook runs at startup and on every skin/dark switch: reuse the plate if
	// it is already mounted. Only this file ever creates a child with that
	// object name, so the static_cast is safe (the class has no Q_OBJECT, so
	// findChild on the concrete type would match any QWidget).
	QWidget* existing = toolBar->findChild<QWidget*>(QLatin1String(kToolbarPlateName), Qt::FindDirectChildrenOnly);
	RackToolbarPlate* plate;
	if (existing != nullptr)
	{
		plate = static_cast<RackToolbarPlate*>(existing);
	}
	else
	{
		plate = new RackToolbarPlate(toolBar);
		// Reserve the rail-ear zones once, at both ends of the action train.
		// The plate toggles these actions with its own visibility, so the
		// zones leave with the chrome on a skin switch and return with it.
		const QList<QAction*> actions = toolBar->actions();
		QAction* leftEar = actions.isEmpty()
			? toolBar->addWidget(makeEarSpacer(toolBar))
			: toolBar->insertWidget(actions.first(), makeEarSpacer(toolBar));
		QAction* rightEar = toolBar->addWidget(makeEarSpacer(toolBar));
		plate->setEarActions(leftEar, rightEar);
	}
	plate->setTokens(tokens);
	plate->showEarZones();
	plate->refreshOverlay();
}

bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens)
{
	const bool ifFamily = info.type == QStringLiteral("if");
	const bool headRow = ifFamily && info.command == QStringLiteral("if");
	const bool branchRow = ifFamily && (info.command == QStringLiteral("elseif") || info.command == QStringLiteral("else"));
	const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
	const int logic = info.logicDepth;
	if (!headRow && logic <= 0)
		return false;

	const bool dark = skinIsDark(tokens);
	// Lane geometry from the row widget; see CommandRowInfo. The branch/tail
	// rows' extra indent unit is already in laneCount.
	const int unit = info.laneUnit;
	const int h = size.height();
	const int junctionY = 4 + tokens.rowHeight / 2;
	const int indentUnits = info.laneCount;
	const int cardLeft = info.cardLeft;

	// The gutter is machined hardware: the bus casing is the rack opening's
	// dark seam, the core is the amber accent, contact blocks and caps are
	// faceplate metal, and the lamps follow the LED palette (green = engaged,
	// danger = evaluation fault). A de-energized run (a swallowed line, a
	// powered-down unit) dims the core instead of raising an alarm.
	const QColor seam(dark ? QStringLiteral("#060809") : QStringLiteral("#8F8268"));
	const QColor amber(tokens.accent);
	const QColor amberDim = mixColor(amber, QColor(tokens.card), 0.55);
	const QColor lampGreen(tokens.accent2);
	const QColor lampDanger(tokens.danger);
	const QColor metal = dark ? QColor(tokens.card).lighter(125) : mixColor(QColor(tokens.card), QColor(tokens.border), 0.35);
	const QColor metalLight = dark ? QColor(tokens.card).lighter(175) : QColor(Qt::white);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(Qt::NoPen);

	const bool live = info.enabled && !info.lineSkipped;
	const auto bandCenter = [&info](int level) { return info.laneCenter(level); };
	const auto busSegment = [&](int level, int y0, int y1, bool segmentLive) {
		const int cx = bandCenter(level);
		painter.fillRect(QRect(cx - 3, y0, 7, y1 - y0), seam);
		painter.fillRect(QRect(cx - 2, y0, 5, y1 - y0), segmentLive ? amber : amberDim);
		painter.fillRect(QRect(cx - 2, y0, 1, y1 - y0), mixColor(segmentLive ? amber : amberDim, QColor(Qt::white), 0.35));
	};
	const auto tapStub = [&](int level, bool stubLive) {
		const int cx = bandCenter(level);
		painter.fillRect(QRect(cx + 3, junctionY - 2, cardLeft - cx - 3, 4), seam);
		painter.fillRect(QRect(cx + 3, junctionY - 1, cardLeft - cx - 3, 2), stubLive ? amber : amberDim);
	};
	const auto contactBlock = [&](const QRect& block) {
		painter.fillRect(block.adjusted(-1, -1, 1, 1), seam);
		painter.fillRect(block, metal);
		painter.fillRect(QRect(block.left(), block.top(), block.width(), 1), metalLight);
	};
	// The relay/pilot lamp in the panel-LED grammar (paintLed) - a bare
	// glowing disc reads as paint, not hardware. Lit green when the branch
	// is taken; an evaluation fault lights the red service bulb; false,
	// short-circuited and not-yet-analyzed all read as the same dark dome.
	const auto lamp = [&](qreal cx, qreal cy, int state, qreal radius) {
		const bool fault = state == 3;
		painter.setRenderHint(QPainter::Antialiasing, true);
		paintLed(painter, QPointF(cx + 0.5, cy + 0.5), radius, fault ? lampDanger : lampGreen, state == 1 || fault, dark);
		painter.setPen(Qt::NoPen);
		painter.setBrush(Qt::NoBrush);
		painter.setRenderHint(QPainter::Antialiasing, false);
	};
	// Outer channel-group levels (levels below the If lanes) keep a quiet
	// dotted rail so a block inside a Channel scope still shows the group.
	const auto channelRail = [&](int level) {
		painter.setPen(QPen(QColor(tokens.border), 1, Qt::DotLine));
		painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
		painter.setPen(Qt::NoPen);
	};

	const int ifLevels = headRow ? logic : (branchRow || tailRow) ? logic - 1 : logic;
	const int channelLevels = qMax(0, indentUnits - ifLevels - ((branchRow || tailRow) ? 1 : 0));
	for (int level = 0; level < channelLevels; level++)
		channelRail(level);

	if (headRow)
	{
		// The relay unit feeds the lane below; the bus only peeks out of the
		// bottom margin, and the lamp on the stub reports the condition.
		for (int level = channelLevels; level < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic;
		busSegment(own, h - 4, h, info.branchState != 0);
		// A small jewel seated in the mounting seam - the feed point's only
		// visible face under the full-width relay unit.
		lamp(bandCenter(own), h - 2.5, info.branchState, 1.4);
	}
	else if (branchRow)
	{
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic - 1;
		busSegment(own, 0, h, true);
		tapStub(own, info.branchState == 1);
		const int cx = bandCenter(own);
		contactBlock(QRect(cx - 3, junctionY - 4, 7, 9));
		lamp(cx, junctionY + 9, info.branchState, 2.2);
	}
	else if (tailRow)
	{
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic - 1;
		busSegment(own, 0, junctionY, true);
		tapStub(own, false);
		const int cx = bandCenter(own);
		contactBlock(QRect(cx - 3, junctionY - 1, 7, 5));
	}
	else
	{
		// A powered unit: tap stub off the innermost bus with a pilot lamp.
		// A swallowed line de-energizes only its own run; the outer lanes
		// stay live for the rows below.
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		busSegment(channelLevels + logic - 1, 0, h, live);
		tapStub(channelLevels + logic - 1, live);
		lamp(cardLeft - 6, junctionY, info.lineSkipped ? 0 : (info.enabled ? 1 : -1), 2.0);
	}
	return true;
}
}
