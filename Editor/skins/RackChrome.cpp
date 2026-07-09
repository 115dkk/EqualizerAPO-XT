/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	QPainter chrome for the "rack" skin. The tiebreaker for every stroke in
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
#include <QToolBar>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "ISkin.h"

namespace
{
const int kEarWidth = 20;
const qreal kNameplateWidth = 78.0;
const qreal kNameplateHeight = 22.0;

bool isDarkPanel(const SkinTokens& tokens)
{
	return QColor(tokens.background).lightness() < 128;
}

QColor withAlpha(const QColor& color, int alpha)
{
	QColor result = color;
	result.setAlpha(alpha);
	return result;
}

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

// Horizontal brushing grain (R4): fine strokes whose ink varies
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
	const bool dark = isDarkPanel(tokens);
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
class RackToolbarPlate : public QWidget
{
public:
	explicit RackToolbarPlate(QToolBar* parentToolBar)
		: QWidget(parentToolBar)
		, toolBar(parentToolBar)
	{
		setObjectName(QLatin1String(kToolbarPlateName));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		toolBar->installEventFilter(this);
		setGeometry(toolBar->rect());
		if (QCheckBox* box = toolBar->findChild<QCheckBox*>(QStringLiteral("InstantModeCheckBox"), Qt::FindDirectChildrenOnly))
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

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == toolBar)
		{
			if (event->type() == QEvent::Resize)
			{
				setGeometry(toolBar->rect());
			}
			else if (event->type() == QEvent::StyleChange)
			{
				// Every skin switch restyles the application; only the rack
				// stylesheet may keep the rail chrome and the ear zones.
				const bool rackActive = SkinManager::instance()->currentSkinId() == QLatin1String("rack");
				setVisible(rackActive);
				if (leftEarAction != nullptr)
					leftEarAction->setVisible(rackActive);
				if (rightEarAction != nullptr)
					rightEarAction->setVisible(rackActive);
			}
		}
		return QWidget::eventFilter(watched, event);
	}

	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		paintToolbarRail(painter, rect(), toolBar, tokens);
	}

private:
	QToolBar* toolBar = nullptr;
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

	const bool dark = isDarkPanel(tokens);
	painter.save();
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

	// Unit seating and bezel (R3): a dark seam runs just inside the QSS
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
		painter.save();
		painter.translate(r.left() + 14.5, r.bottom() - 16);
		painter.rotate(-90);
		painter.setFont(earFont);
		const QRectF textRect(0, -10, r.height() - 64, 20);
		engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, label, withAlpha(QColor(tokens.mutedText), 200), dark);
		painter.restore();
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

	painter.restore();
}

void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	const bool dark = isDarkPanel(tokens);
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
	// carries the value, as on real hardware. R1: the printing must read at
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
	// the two kinds never look alike. R1: the glyphs are engraved (contrast
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
	// hand is on the knob), disabled grays it out. R1: a recessed shadow
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
	// not buildable hardware and it cut the pointer line in half (user
	// direction, AR2 rework round). The value lives in the card's own LED
	// display (EditableValue) beside the knob; state.valueText is
	// deliberately unused here.
}

void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens)
{
	const bool dark = isDarkPanel(tokens);
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMax(2, tokens.borderRadius - 1);
	QPainterPath opening;
	opening.addRoundedRect(r, radius, radius);
	painter.setClipPath(opening);

	// The bay's blank panel is missing, so the opening shows the rack's
	// interior - and the inside of a rack has no finish: it is dark in both
	// modes (the same physical honesty as the display law). The shadow of
	// the unit above hangs over the top of the opening.
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
	// translated (the tooltip carries the accessible caption). At rest the
	// stencil names the state; under the cursor it answers with the action
	// and the amber pre-heat. Always the dark-recess engraving pass: the
	// interior is dark in both finishes.
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

	painter.restore();
}

void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens)
{
	// At rest the seam does not exist - the widget only calls this while
	// hovered, but keep the contract locally honest too.
	if (!state.hovered && !state.pressed)
		return;

	const bool dark = isDarkPanel(tokens);
	painter.save();
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

	painter.restore();
}

void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens)
{
	const bool dark = isDarkPanel(tokens);
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
	plate->lower();
	plate->show();
	plate->update();
}
}
