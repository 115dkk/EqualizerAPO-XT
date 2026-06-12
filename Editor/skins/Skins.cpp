/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include "Editor/skins/RackChrome.h"
#include "Editor/skins/pickers/MatrixFilterPicker.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/CurvedNodeRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"

namespace
{
// Common derived tokens shared by every skin.
void finishTokens(SkinTokens& t)
{
	t.surfaceRaised = t.cardHover;
	t.surfaceSunken = t.graph;
	t.graphGridMajor = t.border;
	if (t.graphGridMinor.isEmpty())
		t.graphGridMinor = t.border;
	t.focusRing = t.accent;
}

// Linear blend between two token colours; t = 0 returns a, t = 1 returns b.
// The Soft skin fakes its elevation steps and pastel arcs by mixing token
// colours instead of introducing extra palette entries, so both modes stay
// consistent with their two-value-step "shadow" rule.
QColor softMix(const QColor& a, const QColor& b, double t)
{
	return QColor(
		qRound(a.red() + (b.red() - a.red()) * t),
		qRound(a.green() + (b.green() - a.green()) * t),
		qRound(a.blue() + (b.blue() - a.blue()) * t));
}

QColor softAlpha(QColor color, int alpha)
{
	color.setAlpha(alpha);
	return color;
}

// ── Studio (glass over the instrument, FabFilter-like) ──────────────────────
// The UI recedes behind the data: deep solid backgrounds, panels as glass
// (solid colour with alpha plus a single 1px lighter top edge), one glowing
// accent. Glow is faked with layered strokes and gradient borders, never
// effects. Tiebreaker: if an element is not the arc, the label or the value,
// it gets removed.

// QSS colour with alpha derived from a token hex string.
QString studioRgba(const QString& hex, double alpha)
{
	const QColor color(hex);
	return QStringLiteral("rgba(%1, %2, %3, %4)")
		.arg(color.red()).arg(color.green()).arg(color.blue())
		.arg(alpha, 0, 'f', 2);
}

QColor studioAlpha(const QString& hex, int alpha)
{
	QColor color(hex);
	color.setAlpha(alpha);
	return color;
}

// The hooks receive tokens but not the mode flag; the background luminance is
// an unambiguous proxy because studio's dark background is near-black.
bool studioIsDark(const SkinTokens& tokens)
{
	return QColor(tokens.background).lightness() < 128;
}

class StudioSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("studio"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/studio_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static CurvedNodeRoutingRenderer renderer;
		return &renderer;
	}

	// "The arc IS the value": no knob body, only a thin track circle, a
	// glowing accent arc from the reference point to the current value and a
	// small indicator dot. Bipolar (gain) knobs anchor at 12 o'clock and grow
	// left or right; unipolar knobs grow from the track start. The numeric
	// readout fades in while hovering or dragging; glow intensifies during a
	// drag; disabled knobs drop to reduced opacity with the glow off.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing);

		// Centred square so the knob stays round in non-square hosts
		// (promoted legacy dials are 100x66, the card knob is 74x74).
		const QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
		const double side = qMin(inner.width(), inner.height());
		const QRectF track(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

		const double span = 270.0;
		const double start = 135.0;     // degrees clockwise from 3 o'clock
		const double ratio = qBound(0.0, state.ratio, 1.0);

		if (!state.enabled)
			painter.setOpacity(0.35);

		// Track: the full range geometry as a thin circle segment.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(tokens.border), 2.0, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(track, qRound(-start * 16), qRound(-span * 16));

		// Reference tick: bipolar knobs mark 0 dB at 12 o'clock so the anchor
		// stays visible even with an empty arc; unipolar knobs carry no tick.
		if (state.bipolar)
		{
			const QPointF top(track.center().x(), track.top());
			painter.setPen(QPen(QColor(tokens.mutedText), 1.5));
			painter.drawLine(QPointF(top.x(), top.y() - 3.0), QPointF(top.x(), top.y() + 3.0));
		}

		double arcFrom = start;
		double sweep = span * ratio;
		if (state.bipolar)
		{
			arcFrom = start + span / 2.0;  // 12 o'clock
			sweep = span * (ratio - 0.5);  // signed: cut grows left, boost right
		}

		// Fake glow: the same arc repainted with wider, more transparent
		// strokes. Hover brightens it, dragging brightens it further.
		const int halo = state.dragging ? 88 : (state.hovered ? 58 : 32);
		const QColor accent(tokens.accent);
		const struct { double width; int alpha; } layers[] = {
			{ 9.0, halo / 3 },
			{ 5.5, halo },
			{ 2.5, 255 }
		};
		for (const auto& layer : layers)
		{
			QColor stroke = accent;
			stroke.setAlpha(layer.alpha);
			painter.setPen(QPen(stroke, layer.width, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(track, qRound(-arcFrom * 16), qRound(-sweep * 16));
		}

		// Indicator dot on the track at the arc end, with its own halo.
		const double endRadians = qDegreesToRadians(-(arcFrom + sweep));
		const QPointF dot(track.center().x() + qCos(endRadians) * side / 2.0,
			track.center().y() - qSin(endRadians) * side / 2.0);
		QColor dotHalo = accent;
		dotHalo.setAlpha(halo);
		painter.setPen(Qt::NoPen);
		painter.setBrush(dotHalo);
		painter.drawEllipse(dot, 6.0, 6.0);
		painter.setBrush(accent);
		painter.drawEllipse(dot, 3.0, 3.0);

		// Keyboard focus: a thin ring just outside the track.
		if (state.focused)
		{
			QColor ring = accent;
			ring.setAlpha(110);
			painter.setPen(QPen(ring, 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(track.adjusted(-4, -4, 4, 4));
		}

		// Numeric readout, mono, fading in on hover and solid while dragging.
		// Only painted when the host supplied a display string (promoted
		// legacy dials show their value in a separate spin box instead).
		if (!state.valueText.isEmpty() && state.enabled && (state.hovered || state.dragging))
		{
			QColor textColor(tokens.text);
			textColor.setAlpha(state.dragging ? 255 : 210);
			painter.setPen(textColor);
			QFont valueFont(tokens.monoFontFamily);
			valueFont.setPointSizeF(qMax(7.0, painter.font().pointSizeF() - 1.0));
			valueFont.setWeight(QFont::DemiBold);
			painter.setFont(valueFont);
			painter.drawText(rect, Qt::AlignCenter, state.valueText);
		}
	}

	// Glass card: solid colour with alpha over the deep background plus a 1px
	// lighter top edge (the reflection). Command types keep one silhouette but
	// announce themselves through the border treatment: DSP rows solid,
	// Include rows dashed (a reference to elsewhere), VST rows a vertical
	// accent gradient (the module radiates its own light). Hover brightens;
	// disabled rows lose the reflection and most of their opacity.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool dark = studioIsDark(tokens);

		if (!info.enabled)
		{
			return QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 8px; }")
				.arg(studioRgba(tokens.card, 0.45), studioRgba(tokens.border, 0.55));
		}

		const QString background = studioRgba(info.selected ? tokens.cardSelected : tokens.card, 0.88);
		const QString hoverBackground = studioRgba(info.selected ? tokens.cardSelected : tokens.cardHover, 0.94);
		const QString topEdge = dark ? QStringLiteral("rgba(255, 255, 255, 0.10)") : QStringLiteral("rgba(255, 255, 255, 0.95)");
		const QString topEdgeHover = dark ? QStringLiteral("rgba(255, 255, 255, 0.17)") : QStringLiteral("#FFFFFF");

		if (info.type == QStringLiteral("vst"))
		{
			// The gradient border is the glow; no separate top edge needed.
			const QString gradient = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
				.arg(studioRgba(tokens.accent, info.focused || info.selected ? 0.95 : 0.70),
					studioRgba(tokens.accent, info.focused || info.selected ? 0.45 : 0.22));
			const QString hoverGradient = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
				.arg(studioRgba(tokens.accent, 0.95), studioRgba(tokens.accent, 0.40));
			return QStringLiteral(
				"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 8px; }"
				" QFrame#FilterCardRow:hover { background: %3; border-color: %4; }")
				.arg(background, gradient, hoverBackground, hoverGradient);
		}

		const QString borderStyle = info.type == QStringLiteral("include")
			? QStringLiteral("dashed") : QStringLiteral("solid");
		const QString borderBrush = info.focused
			? tokens.focusRing
			: (info.selected ? studioRgba(tokens.accent, 0.65) : studioRgba(tokens.border, 0.90));
		const QString hoverBorderBrush = info.focused ? tokens.focusRing : studioRgba(tokens.accent, 0.45);

		return QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-top-color: %4; border-radius: 8px; }"
			" QFrame#FilterCardRow:hover { background: %5; border-color: %6; border-top-color: %7; }")
			.arg(background, borderStyle, borderBrush, topEdge, hoverBackground, hoverBorderBrush, topEdgeHover);
	}

	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool dark = studioIsDark(tokens);
		if (!info.enabled)
		{
			// Disabled: the sheen is off, the header melts into the glass.
			return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-top-left-radius: 8px; border-top-right-radius: 8px; }");
		}
		const QString sheen = dark
			? (info.selected ? QStringLiteral("rgba(255, 255, 255, 0.07)") : QStringLiteral("rgba(255, 255, 255, 0.04)"))
			: (info.selected ? QStringLiteral("rgba(255, 255, 255, 0.75)") : QStringLiteral("rgba(255, 255, 255, 0.55)"));
		return QStringLiteral("QWidget#FilterCardHeader { background: %1; border-top-left-radius: 8px; border-top-right-radius: 8px; }")
			.arg(sheen);
	}

	// Painted decoration on top of the QSS chrome. DSP rows get a short
	// "signal lamp": a glowing accent segment on the left edge, vertically
	// centred on the header strip. VST rows get a halo hugging the border so
	// the module reads as lit from within. Include/comment/raw rows stay
	// unlit; disabled rows paint nothing (the lamp is off).
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		if (!info.enabled)
			return;
		if (info.type == QStringLiteral("spacer") || info.type == QStringLiteral("comment")
			|| info.type == QStringLiteral("text") || info.type == QStringLiteral("include"))
			return;

		painter.save();
		painter.setRenderHint(QPainter::Antialiasing);

		if (info.type == QStringLiteral("vst"))
		{
			// Two strokes hugging the border fake an outer glow without
			// effects; hover turns the light up.
			const QRectF edge = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(studioAlpha(tokens.accent, info.hovered ? 120 : 80), 1.0));
			painter.drawRoundedRect(edge, 8.0, 8.0);
			painter.setPen(QPen(studioAlpha(tokens.accent, info.hovered ? 60 : 36), 3.0));
			painter.drawRoundedRect(edge.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);
		}
		else
		{
			const double segment = 18.0;
			const double y0 = rect.top() + (tokens.rowHeight - segment) / 2.0;
			QLinearGradient lamp(0, y0, 0, y0 + segment);
			QColor mid = studioAlpha(tokens.accent, info.hovered ? 235 : 185);
			QColor fade = studioAlpha(tokens.accent, 0);
			lamp.setColorAt(0.0, fade);
			lamp.setColorAt(0.5, mid);
			lamp.setColorAt(1.0, fade);
			painter.setPen(Qt::NoPen);
			// Bloom first (wider, fainter), then the lamp core on the edge.
			QLinearGradient bloom(0, y0 - 3.0, 0, y0 + segment + 3.0);
			bloom.setColorAt(0.0, fade);
			bloom.setColorAt(0.5, studioAlpha(tokens.accent, info.hovered ? 70 : 42));
			bloom.setColorAt(1.0, fade);
			painter.fillRect(QRectF(rect.left(), y0 - 3.0, 4.0, segment + 6.0), bloom);
			painter.fillRect(QRectF(rect.left(), y0, 2.0, segment), lamp);
		}

		painter.restore();
	}

	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 8;
		t.rowHeight = 40;
		t.channelGroupIndent = 18;
		t.channelGroupStyle = SkinTokens::GradientBar;
		t.badgeStyle = SkinTokens::ColorPill;
		if (dark)
		{
			t.background = QStringLiteral("#070A12");
			t.surface = QStringLiteral("#0D1322");
			t.card = QStringLiteral("#121A2C");
			t.cardHover = QStringLiteral("#182238");
			t.cardSelected = QStringLiteral("#1E3158");
			t.text = QStringLiteral("#E8EEFB");
			t.mutedText = QStringLiteral("#91A0BA");
			t.border = QStringLiteral("#26324A");
			t.graph = QStringLiteral("#060914");
			t.graphGridMinor = QStringLiteral("#26324A");
			t.accent = QStringLiteral("#5B8CFF");
			t.accent2 = QStringLiteral("#A66CFF");
		}
		else
		{
			t.background = QStringLiteral("#EEF2F8");
			t.surface = QStringLiteral("#F8FAFE");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#F3F6FC");
			t.cardSelected = QStringLiteral("#DDE8FF");
			t.text = QStringLiteral("#182033");
			t.mutedText = QStringLiteral("#66728A");
			t.border = QStringLiteral("#D8E0EF");
			t.graph = QStringLiteral("#F6F7FB");
			t.graphGridMinor = QStringLiteral("#D8E0EF");
			t.accent = QStringLiteral("#2F6BFF");
			t.accent2 = QStringLiteral("#8A4DFF");
		}
		finishTokens(t);
		return t;
	}
};

// ── Minimal ("The bank teller's terminal") helpers ──────────────────────────

// Screen point on a circle around center; degrees follow the shared knob
// convention (Qt-style angles, counter-clockwise from 3 o'clock, screen Y
// grows downward so sin is subtracted). Pass the negated clockwise sweep
// angle, exactly like ISkin.cpp's default renderer does.
QPointF minimalPointOnArc(const QPointF& center, double radius, double degrees)
{
	const double radians = qDegreesToRadians(degrees);
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}

// ANNEX K minimal: "the number is the control". The always-visible mono
// numeric is primary; beside it sits a small flat circle with a 1px indicator
// line and a hairline range arc, monochrome until dragged (accent while
// active). Bipolar knobs measure the indicator from a centre tick. Promoted
// legacy dials supply no valueText (their number lives in the adjacent spin
// box), so they render only the confirmation circle plus a ratio-derived
// position readout while hovered or dragged.
void paintMinimalKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor hairline(tokens.border);
	const QColor primary(state.enabled ? tokens.text : tokens.mutedText);
	const QColor secondary(tokens.mutedText);
	const QColor active(tokens.accent);
	const QColor indicatorColor = (state.enabled && state.dragging) ? active : primary;

	const bool hasNumber = !state.valueText.isEmpty();
	const double circleRadius = hasNumber ? 9.0 : 12.0;
	const double arcRadius = circleRadius + 4.0;

	QFont numberFont(tokens.monoFontFamily);
	numberFont.setBold(true);
	numberFont.setPointSizeF(9.0);

	QPointF circleCenter;
	QRectF numberRect;
	if (hasNumber)
	{
		// Number left (primary), confirmation circle beside it; the pair is
		// centred in the widget. Shrink the font instead of clipping when a
		// long value (e.g. "-100.0") meets a narrow widget.
		const double gap = 6.0;
		double available = rect.width() - 2.0 * arcRadius - gap - 4.0;
		double textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		while (textWidth > available && numberFont.pointSizeF() > 6.5)
		{
			numberFont.setPointSizeF(numberFont.pointSizeF() - 0.5);
			textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		}
		const double pairWidth = textWidth + gap + 2.0 * arcRadius;
		const double left = rect.left() + (rect.width() - pairWidth) / 2.0;
		numberRect = QRectF(left, rect.top(), textWidth, rect.height());
		circleCenter = QPointF(left + textWidth + gap + arcRadius, QRectF(rect).center().y());
	}
	else
	{
		// Circle only; keep a constant bottom strip free for the hover/drag
		// readout so the circle does not jump when the readout appears.
		circleCenter = QPointF(QRectF(rect).center().x(), rect.top() + (rect.height() - 14.0) / 2.0);
	}

	// Hairline range arc: the full 270-degree value range, 1px.
	const QRectF arcRect(circleCenter.x() - arcRadius, circleCenter.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(arcRect, -135 * 16, -270 * 16);

	if (state.bipolar)
	{
		// Centre tick at 12 o'clock (the bipolar neutral) and a 1px deviation
		// arc measured from it: boost grows clockwise, cut counter-clockwise.
		// Unipolar knobs draw neither, so the two kinds read differently.
		painter.setPen(QPen(secondary, 1));
		painter.drawLine(minimalPointOnArc(circleCenter, arcRadius - 1.5, -270.0),
			minimalPointOnArc(circleCenter, arcRadius + 2.5, -270.0));
		const double deviationDegrees = 270.0 * (state.ratio - 0.5);
		painter.setPen(QPen(indicatorColor, 1));
		painter.drawArc(arcRect, -270 * 16, -qRound(deviationDegrees * 16.0));
	}

	// Small flat circle: flat fill, 1px border, hover = one background step.
	const QString fill = (state.enabled && (state.hovered || state.dragging)) ? tokens.cardHover : tokens.card;
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(QColor(fill));
	painter.drawEllipse(circleCenter, circleRadius, circleRadius);

	// 1px indicator line from the hub to the rim at the value angle.
	const double valueDegrees = -(135.0 + 270.0 * state.ratio);
	painter.setPen(QPen(indicatorColor, 1));
	painter.drawLine(circleCenter, minimalPointOnArc(circleCenter, circleRadius - 1.0, valueDegrees));

	if (hasNumber)
	{
		painter.setFont(numberFont);
		painter.setPen((state.enabled && state.dragging) ? active : primary);
		painter.drawText(numberRect, Qt::AlignVCenter | Qt::AlignLeft, state.valueText);
	}
	else if (state.enabled && (state.hovered || state.dragging))
	{
		// No supplied value text: show the dial position derived from ratio.
		// The real value sits in the adjacent spin box, so a percentage is the
		// only honest readout for log-scaled legacy dials.
		QFont readoutFont(tokens.monoFontFamily);
		readoutFont.setPointSizeF(7.5);
		painter.setFont(readoutFont);
		painter.setPen(state.dragging ? active : secondary);
		const QRectF readoutRect(rect.left(), rect.bottom() - 14.0, rect.width(), 14.0);
		painter.drawText(readoutRect, Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(state.ratio * 100.0)));
	}

	// Keyboard focus: a square hairline frame (radius 0 corner language).
	if (state.focused)
	{
		painter.setPen(QPen(QColor(tokens.focusRing), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
	}
}

// Leading type glyph for the line head, plain ASCII for the mapped types so
// every mono fallback font covers it. The glyph is shape information ("which
// kind of line is this"); the colour tag next to it stays the badge token.
QString minimalTypeGlyph(const QString& type)
{
	if (type == QStringLiteral("biquad"))
		return QStringLiteral("~");
	if (type == QStringLiteral("include"))
		return QStringLiteral(">>");
	if (type == QStringLiteral("vst"))
		return QStringLiteral("[]");
	if (type == QStringLiteral("copy"))
		return QStringLiteral("->");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	if (type == QStringLiteral("spacer"))
		return QString();
	// Unmapped commands keep the fixed-width column so line heads stay
	// aligned; the middle dot (U+00B7) deliberately carries no further
	// meaning. Built from a code point so the source stays pure ASCII (no
	// /utf-8 flag is set for MSVC).
	return QString(QChar(0x00B7));
}

// ── Minimal (Ableton-like terminal, monospace) ──────────────────────────────
class MinimalSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("minimal"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/precision_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static StepListRoutingRenderer renderer;
		return &renderer;
	}
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		paintMinimalKnob(painter, rect, state, tokens);
	}
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// One flat line per command: 1px hairline box, square corners, and
		// state expressed as background-value steps only. Disabled rows fall
		// one step below the resting card; selection is the accent-value
		// background step; hover is exactly one step up from rest.
		const QString background = !info.enabled ? tokens.surface
			: (info.selected ? tokens.cardSelected : tokens.card);
		const QString borderColor = info.focused ? tokens.focusRing
			: (info.selected ? tokens.accent : tokens.border);
		QString style = QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 0; }")
			.arg(background, borderColor);
		if (!info.selected)
		{
			style += QStringLiteral(" QFrame#FilterCardRow:hover { background: %1; }")
				.arg(!info.enabled ? tokens.card : tokens.cardHover);
		}
		return style;
	}
	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		// No separate header plate: the row reads as a single text line, so
		// the header inherits the frame's background through transparency.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0; }");
	}
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		Q_UNUSED(body);
		// Leading type glyph at the line head. Only modern card rows carry a
		// header here; the Include/VST body editors and the frozen legacy
		// rows consult the hook with header == nullptr and stay untouched.
		if (header == nullptr)
			return;
		QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
		if (headerLayout == nullptr)
			return;
		// A commented-out line leads with the comment marker it actually
		// carries in the config file; the marker is information, not decor.
		const QString glyph = info.enabled ? minimalTypeGlyph(info.type) : QStringLiteral("#");
		if (glyph.isEmpty())
			return;
		QLabel* glyphLabel = new QLabel(glyph, header);
		glyphLabel->setObjectName(QStringLiteral("MinimalTypeGlyph"));
		glyphLabel->setAlignment(Qt::AlignCenter);
		glyphLabel->setMinimumWidth(18);
		headerLayout->insertWidget(0, glyphLabel);
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.accent = QStringLiteral("#3B82F6");
		t.fontFamily = QStringLiteral("DM Mono");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 0;
		t.rowHeight = 32;
		t.channelGroupIndent = 16;
		t.channelGroupStyle = SkinTokens::TreeLines;
		t.badgeStyle = SkinTokens::OutlineOnly;
		t.zebraStripe = true;
		if (dark)
		{
			t.background = QStringLiteral("#191919");
			t.surface = QStringLiteral("#1f1f1f");
			t.card = QStringLiteral("#262626");
			t.cardHover = QStringLiteral("#2c2c2c");
			t.cardSelected = QStringLiteral("#1f3554");
			t.text = QStringLiteral("#cccccc");
			t.mutedText = QStringLiteral("#777777");
			t.border = QStringLiteral("#3c3c3c");
			t.graph = QStringLiteral("#0e0e0e");
			t.graphGridMajor = QStringLiteral("#383838");
			t.graphGridMinor = QStringLiteral("#2c2c2c");
		}
		else
		{
			t.background = QStringLiteral("#F6F6F3");
			t.surface = QStringLiteral("#FFFFFF");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#F0F0EC");
			t.cardSelected = QStringLiteral("#E8F1FF");
			t.text = QStringLiteral("#202020");
			t.mutedText = QStringLiteral("#666660");
			t.border = QStringLiteral("#D2D2CC");
			t.graph = QStringLiteral("#FFFFFF");
			t.graphGridMajor = QStringLiteral("#D2D2CC");
			t.graphGridMinor = QStringLiteral("#E6E6E0");
		}
		finishTokens(t);
		return t;
	}
};

// ── Soft ("The iPhone settings screen": macOS System Settings calm) ─────────
// Round, roomy, impossible to fear. Shadows are faked with two background
// value steps plus a very light 1px border; hierarchy comes from size and
// whitespace, never from density. Hover lifts a surface exactly one value
// step. Tiebreaker: when in doubt, remove elements and add whitespace.
class SoftSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("soft"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/soft_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static BlockChipRoutingRenderer renderer;
		return &renderer;
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.accent = QStringLiteral("#3B82F6");
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		// Constitution: radius 10-12px, generous line spacing. The tallest row
		// of the five skins; whitespace is the hierarchy device.
		t.borderRadius = 12;
		t.rowHeight = 48;
		t.channelGroupIndent = 20;
		t.density = 2;
		t.channelGroupStyle = SkinTokens::SoftShadow;
		t.badgeStyle = SkinTokens::SoftPill;
		// Tiebreaker rule applied: the raw monospace preview strip under every
		// card is exactly the kind of element that makes a screen feel anxious,
		// so this skin removes it and keeps the whitespace.
		t.showRawPreview = false;
		if (dark)
		{
			t.background = QStringLiteral("#171923");
			t.surface = QStringLiteral("#202433");
			t.card = QStringLiteral("#282D3E");
			t.cardHover = QStringLiteral("#30364A");
			t.cardSelected = QStringLiteral("#344065");
			t.text = QStringLiteral("#F2F4FA");
			t.mutedText = QStringLiteral("#A7AEC2");
			t.border = QStringLiteral("#3A4056");
			t.graph = QStringLiteral("#151925");
		}
		else
		{
			t.background = QStringLiteral("#F7F4EF");
			t.surface = QStringLiteral("#FFFDF9");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#FFF7EC");
			t.cardSelected = QStringLiteral("#EEF2FF");
			t.text = QStringLiteral("#28231F");
			t.mutedText = QStringLiteral("#786F67");
			t.border = QStringLiteral("#E9DED1");
			t.graph = QStringLiteral("#FFFAF3");
		}
		finishTokens(t);
		return t;
	}

	// One calm silhouette for every command type: a 12px rounded card one
	// value step above the window, "shadowed" only by that step and a very
	// light 1px border. Hover lifts the whole card one more value step
	// (QSS :hover re-evaluates at paint time, so the inline rule is enough).
	// A commented-out row sinks flush into the window background and keeps
	// only a dashed outline - an empty slot, not an alarm.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& t) const override
	{
		if (!info.enabled)
		{
			return QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px dashed %2; border-radius: %3px; }")
				.arg(t.background, t.border)
				.arg(t.borderRadius);
		}

		const QString borderColor = info.focused ? t.focusRing : (info.selected ? t.accent : t.border);
		const QString backgroundColor = info.selected ? t.cardSelected : t.card;
		const QString hoverColor = info.selected ? t.cardSelected : t.cardHover;
		return QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
			"QFrame#FilterCardRow:hover { background: %4; }")
			.arg(backgroundColor, borderColor)
			.arg(t.borderRadius)
			.arg(hoverColor);
	}

	// No header strip: the header shares the card surface so the row reads as
	// one roomy rounded object (macOS System Settings rows have no banded
	// title bar). Hierarchy inside the header comes from type tile, title size
	// and whitespace, all handled in the QSS sheets.
	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
	}

	// Annex K, soft: "a handle you cannot fumble". The largest knob of the
	// five skins. Two-step elevation body, rounded dot indicator (no sharp
	// line), pastel range arc, value in a rounded badge below, centre detent
	// as a gentle notch. Bipolar knobs grow their arc from the 12 o'clock
	// detent (boost right in accent, cut left in accent2); unipolar knobs
	// grow from the minimum, so the two kinds differ at a glance.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing);

		const QColor card(tokens.card);
		const QColor windowBg(tokens.background);
		const QColor border(tokens.border);
		const QColor muted(tokens.mutedText);

		// Reserve a strip at the bottom for the rounded value badge so it sits
		// below the handle instead of floating on the face. Promoted legacy
		// dials hand in an empty valueText (their value lives in a spin box),
		// so they keep the full height for the handle.
		const bool hasBadge = !state.valueText.isEmpty();
		QRectF area(rect);
		qreal badgeHeight = 0;
		if (hasBadge)
		{
			badgeHeight = qMin<qreal>(18.0, area.height() * 0.26);
			area.setBottom(area.bottom() - badgeHeight - 1.0);
		}

		// Largest knob of the five: only a 4px inset, centred square so the
		// handle stays circular in the 100x66 legacy dial slots.
		QRectF inner = area.adjusted(4, 4, -4, -4);
		const double side = qMin(inner.width(), inner.height());
		QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

		const int spanDegrees = 270;
		const int startDegrees = 135;
		const double ratio = qBound(0.0, state.ratio, 1.0);
		const double endDegrees = startDegrees + spanDegrees * ratio;

		const double arcWidth = qMax(5.0, side * 0.10);
		QRectF arcRect = knobRect.adjusted(arcWidth / 2.0, arcWidth / 2.0, -arcWidth / 2.0, -arcWidth / 2.0);

		// Keyboard focus: a quiet halo around the whole handle, not a hard ring.
		if (state.focused && state.enabled)
		{
			painter.setPen(QPen(softAlpha(QColor(tokens.focusRing), 90), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(knobRect.adjusted(-2, -2, 2, 2));
		}

		// Pastel range track: the full travel is always visible.
		const QColor trackColor = state.enabled ? softMix(border, windowBg, 0.25) : softAlpha(border, 110);
		painter.setPen(QPen(trackColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);

		// Pastel value arc (accent softened one step toward the card colour).
		if (state.enabled)
		{
			const bool cutSide = state.bipolar && ratio < 0.5;
			const QColor valueColor = softMix(QColor(cutSide ? tokens.accent2 : tokens.accent), card, 0.30);
			painter.setPen(QPen(valueColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
			if (state.bipolar)
			{
				const double centerDegrees = startDegrees + spanDegrees / 2.0;
				painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-(endDegrees - centerDegrees) * 16.0));
			}
			else
			{
				painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees * ratio * 16.0));
			}
		}

		// Two-step elevation body: a base disc one value step below the face,
		// then the face one step above with a very light 1px border. Hover
		// lifts the face exactly one value step; no real shadow effects.
		const double faceInset = arcWidth + 2.5;
		QRectF baseRect = knobRect.adjusted(faceInset, faceInset, -faceInset, -faceInset);
		painter.setPen(Qt::NoPen);
		painter.setBrush(softMix(card, windowBg, 0.55));
		painter.drawEllipse(baseRect);

		QColor faceColor = card;
		if (!state.enabled)
			faceColor = softMix(card, windowBg, 0.5);
		else if (state.hovered || state.dragging)
			faceColor = QColor(tokens.cardHover);
		QRectF faceRect = baseRect.adjusted(2.5, 2.5, -2.5, -2.5);
		painter.setPen(QPen(border, 1));
		painter.setBrush(faceColor);
		painter.drawEllipse(faceRect);

		// Centre detent as a gentle notch: a short rounded tick at 12 o'clock
		// on the face, only for bipolar (gain) knobs where the centre means
		// 0 dB.
		if (state.bipolar)
		{
			painter.setPen(QPen(softAlpha(muted, state.enabled ? 170 : 90), 2, Qt::SolidLine, Qt::RoundCap));
			const QPointF center = faceRect.center();
			const double notchOuter = faceRect.width() / 2.0 - 2.0;
			const double notchInner = qMax(0.0, notchOuter - 4.5);
			painter.drawLine(QPointF(center.x(), center.y() - notchOuter), QPointF(center.x(), center.y() - notchInner));
		}

		// Rounded dot indicator instead of a sharp line; it grows slightly on
		// hover and again while dragging, the calmest possible "I am held" cue.
		double dotRadius = qMax(3.5, side * 0.065);
		if (state.dragging)
			dotRadius += 1.0;
		else if (state.hovered)
			dotRadius += 0.5;
		const double dotTrack = faceRect.width() / 2.0 - dotRadius - 2.5;
		const double radians = qDegreesToRadians(-endDegrees);
		const QPointF dotPos(faceRect.center().x() + qCos(radians) * dotTrack,
			faceRect.center().y() - qSin(radians) * dotTrack);
		painter.setPen(Qt::NoPen);
		painter.setBrush(state.enabled ? QColor(tokens.accent) : softAlpha(muted, 120));
		painter.drawEllipse(dotPos, dotRadius, dotRadius);

		// Value in a rounded badge below the handle.
		if (hasBadge)
		{
			QFont badgeFont = painter.font();
			badgeFont.setWeight(QFont::DemiBold);
			badgeFont.setPointSizeF(qMax(7.0, badgeFont.pointSizeF() - 1.5));
			painter.setFont(badgeFont);
			const QFontMetricsF metrics(badgeFont);
			const qreal badgeWidth = qMin<qreal>(QRectF(rect).width(), metrics.horizontalAdvance(state.valueText) + 14.0);
			QRectF badgeRect(QRectF(rect).center().x() - badgeWidth / 2.0,
				QRectF(rect).bottom() - badgeHeight - 0.5, badgeWidth, badgeHeight);
			painter.setPen(QPen(border, 1));
			painter.setBrush(state.enabled ? QColor(tokens.surfaceRaised) : softMix(card, windowBg, 0.5));
			painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
			painter.setPen(state.enabled ? QColor(tokens.text) : muted);
			painter.drawText(badgeRect, Qt::AlignCenter, state.valueText);
		}
	}
};

// ── Rack ("The amplifier faceplate", skeuomorphic 19" hardware) ─────────────
class RackSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("rack"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/rack_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static HardwarePatchbayRoutingRenderer renderer;
		return &renderer;
	}

	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		RackChrome::paintKnob(painter, rect, state, tokens);
	}

	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// QSS only provides the machined base plate and the hover brightening;
		// the faceplate texture, ears, screws and LEDs are painted on top by
		// RackChrome::paintCardChrome (the sheen overlays are translucent, so
		// the hover state shines through them).
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
		const QString background = info.selected ? tokens.cardSelected : tokens.card;
		return QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
			"QFrame#FilterCardRow:hover { background: %4; }")
			.arg(background, borderColor)
			.arg(tokens.borderRadius)
			.arg(info.selected ? tokens.cardSelected : tokens.cardHover);
	}

	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		// The header strip is part of the painted faceplate; a transparent
		// background lets the brushed metal, ears and LEDs show through.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
	}

	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		// Reserve the rack-ear zones along the faceplate edges so the painted
		// chrome (screws, LEDs, patchbay jacks, the VST nameplate) never
		// collides with row content. Rows are rebuilt on every skin switch, so
		// this only ever runs while the rack skin is active.
		if (header != nullptr && header->layout() != nullptr)
		{
			const int right = RackChrome::earWidth() + 6
				+ (info.type == QStringLiteral("vst") ? RackChrome::nameplateReserve() : 0);
			header->layout()->setContentsMargins(RackChrome::earWidth() + 6, 4, right, 4);
		}
		// Only the modern card's body stack is inset; body-only consultations
		// (Include/VST editors, legacy rows) already sit inside that stack.
		if (card != nullptr && body != nullptr)
			body->setContentsMargins(RackChrome::earWidth() + 4, 0, RackChrome::earWidth() + 4, 6);
	}

	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		RackChrome::paintCardChrome(painter, rect, info, tokens);
	}

	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		// Machined plate corners; raw config lines stay off the faceplate (the
		// "..." raw editor still reaches them) - hardware prints no raw text.
		t.borderRadius = 3;
		t.showRawPreview = false;
		t.rowHeight = 36;
		t.channelGroupIndent = 16;
		t.channelGroupStyle = SkinTokens::DottedLine;
		t.badgeStyle = SkinTokens::WireframeBorder;
		t.accent = dark ? QStringLiteral("#F4B860") : QStringLiteral("#B66A00");
		t.accent2 = dark ? QStringLiteral("#5ED0A0") : QStringLiteral("#177A55");
		if (dark)
		{
			t.background = QStringLiteral("#0B0D0F");
			t.surface = QStringLiteral("#14181C");
			t.card = QStringLiteral("#1D2328");
			t.cardHover = QStringLiteral("#252B2F");
			t.cardSelected = QStringLiteral("#332718");
			t.text = QStringLiteral("#E6E0D4");
			t.mutedText = QStringLiteral("#9A9488");
			t.border = QStringLiteral("#3A4248");
			t.graph = QStringLiteral("#060807");
			t.graphGridMinor = QStringLiteral("#1F3A31");
		}
		else
		{
			t.background = QStringLiteral("#E7E2D8");
			t.surface = QStringLiteral("#F4EFE5");
			t.card = QStringLiteral("#FFFAEF");
			t.cardHover = QStringLiteral("#F7EEDC");
			t.cardSelected = QStringLiteral("#FCE8BD");
			t.text = QStringLiteral("#2B2721");
			t.mutedText = QStringLiteral("#746A5D");
			t.border = QStringLiteral("#C9BFAE");
			t.graph = QStringLiteral("#FFF7E6");
			t.graphGridMinor = QStringLiteral("#D6C4A6");
		}
		finishTokens(t);
		return t;
	}
};

// ── Matrix (signal-routing matrix / departure board) ────────────────────────
//
// Constitution: an airport departure board / broadcast routing matrix.
// Everything aligns to a grid; numeric values are monospace; 1px rules; rows
// behave like cells with coordinates. Traffic-light colours (green/amber/red)
// are reserved for status and never used decoratively. Hover highlights the
// row band and the coordinate-column band (crosspoint feel). Disabled rows get
// a dashed border. Knobs are rotary encoders with a stepped LED ring and a
// boxed mono numeric cell as the authoritative reading.

namespace MatrixMetrics
{
// The grid the whole skin aligns to (card grid pitch, coordinate column).
constexpr int gridPitch = 24;
// Width of the coordinate-column band (expand + number + type cells of the
// header) used by the crosspoint hover highlight.
constexpr int coordinateBandWidth = 120;
// Left inset of the card content: 3px status rail (border-left) + 1px gutter.
constexpr int railInset = 4;
// Height of the boxed numeric readout cell under a card knob.
constexpr int knobCellHeight = 16;
}

namespace
{
// Point on the 270-degree value arc; fraction 0 is bottom-left (7:30), 0.5 is
// 12 o'clock, 1 is bottom-right (4:30). Same sweep as the shared default knob.
QPointF matrixRadialPoint(const QPointF& center, double radius, double fraction)
{
	const double radians = qDegreesToRadians(-(135.0 + 270.0 * fraction));
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}
}

class MatrixSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("matrix"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/matrix_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static CrosspointMatrixRoutingRenderer renderer;
		return &renderer;
	}
	// The "add filter" picker as a two-axis selection instrument: a bus rail
	// of categories and a column of coordinate-labelled entry cells, engaged
	// like a crosspoint (MatrixFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new MatrixFilterPickerView(parent);
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		// Square cells and 1px rules only; 36px rows keep the board dense and
		// on the 12px grid (gridPitch 24 = two rows of 12).
		t.borderRadius = 0;
		t.rowHeight = 36;
		t.channelGroupIndent = 24;
		t.channelGroupStyle = SkinTokens::GradientBar;
		t.badgeStyle = SkinTokens::OutlineOnly;
		t.cardRailWidth = 3;
		t.accent = dark ? QStringLiteral("#22D3EE") : QStringLiteral("#008EAA");
		t.accent2 = dark ? QStringLiteral("#7CFFB2") : QStringLiteral("#0A8F57");
		if (dark)
		{
			t.background = QStringLiteral("#060B10");
			t.surface = QStringLiteral("#0B141C");
			t.card = QStringLiteral("#101B25");
			t.cardHover = QStringLiteral("#142432");
			t.cardSelected = QStringLiteral("#082B34");
			t.text = QStringLiteral("#DFF5FF");
			t.mutedText = QStringLiteral("#7FA0AE");
			t.border = QStringLiteral("#233443");
			t.graph = QStringLiteral("#041018");
			t.graphGridMinor = QStringLiteral("#183443");
		}
		else
		{
			t.background = QStringLiteral("#F0F6F8");
			t.surface = QStringLiteral("#FFFFFF");
			t.card = QStringLiteral("#F9FCFD");
			t.cardHover = QStringLiteral("#EDF7FA");
			t.cardSelected = QStringLiteral("#D7F8FF");
			t.text = QStringLiteral("#10242F");
			t.mutedText = QStringLiteral("#5F7782");
			t.border = QStringLiteral("#D4E2E8");
			t.graph = QStringLiteral("#F9FCFD");
			t.graphGridMinor = QStringLiteral("#D4E2E8");
			// Traffic-light status colours tuned for contrast on light surfaces.
			t.success = QStringLiteral("#15803D");
			t.warning = QStringLiteral("#B45309");
			t.danger = QStringLiteral("#DC2626");
		}
		finishTokens(t);
		return t;
	}

	// Rotary encoder with an LED ring: the value reads as discrete lit
	// segments, the exact value as text in a boxed mono cell. Bipolar knobs
	// light segments left or right of a centre gap (12 o'clock detent);
	// unipolar knobs fill clockwise from the minimum.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		const QColor borderColor(tokens.border);
		const QColor accentColor(tokens.accent);
		const QColor cutColor(tokens.accent2);
		const QColor mutedColor(tokens.mutedText);

		// Reserve the bottom strip for the boxed numeric cell when the widget
		// supplies an authoritative value text (e.g. the Preamp card). Promoted
		// legacy dials show their value in the adjacent spin box instead.
		QRect knobArea = rect;
		if (!state.valueText.isEmpty())
			knobArea.adjust(0, 0, 0, -(MatrixMetrics::knobCellHeight + 2));

		QRectF inner = QRectF(knobArea).adjusted(5, 5, -5, -5);
		const double side = qMin(inner.width(), inner.height());
		const QRectF ringRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
		const QPointF center = ringRect.center();
		const double outerRadius = side / 2.0;
		const double innerRadius = qMax(outerRadius - 6.0, 1.0);
		const double bodyRadius = qMax(innerRadius - 3.0, 1.0);

		painter.setRenderHint(QPainter::Antialiasing);

		// Segment ring. An even count gives bipolar knobs a natural centre gap
		// at 12 o'clock; unipolar knobs use an odd count so a segment sits at
		// every position including the centre.
		const int segmentCount = state.bipolar ? 14 : 15;
		const int half = segmentCount / 2;
		int litFrom = 0;
		int litCount = 0;
		bool boost = true;
		if (state.bipolar)
		{
			const double deviation = state.ratio - 0.5;
			boost = deviation >= 0.0;
			litCount = qMin(half, qRound(qAbs(deviation) * 2.0 * half));
			litFrom = boost ? half : half - litCount;
		}
		else
		{
			litCount = qBound(0, qRound(state.ratio * segmentCount), segmentCount);
		}

		QColor litColor = state.bipolar && !boost ? cutColor : accentColor;
		if (state.dragging)
			litColor = litColor.lighter(130);
		else if (state.hovered)
			litColor = litColor.lighter(115);
		QColor trackColor = borderColor;
		trackColor.setAlpha(state.enabled ? 170 : 70);

		for (int i = 0; i < segmentCount; i++)
		{
			const double fraction = (i + 0.5) / segmentCount;
			const bool lit = state.enabled && i >= litFrom && i < litFrom + litCount;
			QPen segmentPen(lit ? litColor : trackColor, 3.0, Qt::SolidLine, Qt::FlatCap);
			painter.setPen(segmentPen);
			painter.drawLine(matrixRadialPoint(center, innerRadius, fraction),
				matrixRadialPoint(center, outerRadius, fraction));
		}

		// Centre detent tick: marks the 0-position gap of bipolar knobs so the
		// two knob kinds read differently even at rest.
		if (state.bipolar)
		{
			painter.setPen(QPen(state.enabled ? mutedColor : trackColor, 1.0, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(matrixRadialPoint(center, outerRadius + 1.0, 0.5),
				matrixRadialPoint(center, outerRadius + 4.0, 0.5));
		}

		// Encoder body and pointer.
		QColor bodyColor(state.enabled ? tokens.card : tokens.surface);
		painter.setPen(QPen(borderColor, 1.0, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(bodyColor);
		painter.drawEllipse(center, bodyRadius, bodyRadius);
		painter.setPen(QPen(state.enabled ? litColor : QColor(mutedColor), 2.0, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(matrixRadialPoint(center, bodyRadius * 0.45, state.ratio),
			matrixRadialPoint(center, bodyRadius - 1.5, state.ratio));

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Keyboard focus: a square cell bracket, not a glow - this skin's
		// corner language is the rectangle.
		if (state.focused && state.enabled)
		{
			painter.setPen(QPen(accentColor, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(ringRect.toRect().adjusted(-3, -3, 3, 3));
		}

		// Boxed mono numeric cell: the authoritative reading.
		if (!state.valueText.isEmpty())
		{
			QFont monoFont(tokens.monoFontFamily);
			monoFont.setPointSizeF(7.5);
			monoFont.setBold(true);
			const QFontMetrics metrics(monoFont);
			const int cellWidth = qMin(rect.width(), metrics.horizontalAdvance(state.valueText) + 12);
			const QRect cellRect(rect.center().x() - cellWidth / 2,
				rect.bottom() - MatrixMetrics::knobCellHeight + 1, cellWidth, MatrixMetrics::knobCellHeight - 1);
			painter.setPen(QPen(state.dragging ? accentColor : borderColor, 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
			painter.drawRect(cellRect);
			painter.setFont(monoFont);
			if (!state.enabled)
				painter.setPen(QColor(mutedColor));
			else if (state.dragging || state.hovered)
				painter.setPen(accentColor);
			else
				painter.setPen(QColor(tokens.text));
			painter.drawText(cellRect, Qt::AlignCenter, state.valueText);
		}
	}

	// Departure-board cell: square corners, 1px rule, and a 3px status rail in
	// traffic-light semantics (green = active, amber = bypassed). A commented
	// out row additionally swaps the outer rule for a dashed one.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const QString railColor = info.enabled ? tokens.success : tokens.warning;
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
		const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
		const QString borderStyle = info.enabled ? QStringLiteral("solid") : QStringLiteral("dashed");
		QString style = QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-left: 3px solid %4; border-radius: 0px; }")
			.arg(backgroundColor, borderStyle, borderColor, railColor);
		// The :hover rule both signals the row crosspoint and makes Qt repaint
		// the frame on enter/leave, which drives the painted column band.
		style += QStringLiteral(
			" QFrame#FilterCardRow:hover { border: 1px %1 %2; border-left: 3px solid %3; }")
			.arg(borderStyle, tokens.accent, railColor);
		return style;
	}

	// The header strip stays transparent: paintCardChrome owns the band fill,
	// the 1px header rule and the faint column grid behind it.
	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		Q_UNUSED(info);
		Q_UNUSED(tokens);
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0px; }");
	}

	// Monochrome type cell: the command type reads from the mono code text
	// (BQUAD/INC/VST/...), not from a per-type colour. Traffic-light colours
	// stay reserved for status.
	QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& tokens) const override
	{
		Q_UNUSED(typeColor);
		const QString ink = info.enabled ? tokens.text : tokens.mutedText;
		return QStringLiteral("color:%1; border-color:%2; background-color:transparent;")
			.arg(ink, tokens.border);
	}

	// Per-type body treatments. The frozen legacy rows keep their stock
	// construction; only the modern card editors are decorated.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		Q_UNUSED(header);
		if (info.legacyRow || body == nullptr)
			return;

		if (info.type == QStringLiteral("include"))
		{
			// Include row: a single "> source: <path>" board line. The stock
			// file icon becomes a mono feed marker (QSS #IncludeCardGlyph
			// styles it); the path field is already the <path> cell. ASCII ">"
			// instead of U+25B8: DM Mono has no glyph for the triangle and the
			// offscreen platform renders tofu.
			QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("IncludeCardGlyph"));
			if (glyph != nullptr)
				glyph->setText(QStringLiteral("> source:"));
		}
		else if (info.type == QStringLiteral("vst") && body->objectName() == QStringLiteral("VSTCardEditor"))
		{
			// VST row: an external-device entry. A port strip with IN/OUT
			// markings heads the body, so the plugin reads as outboard gear
			// patched into the signal path.
			QVBoxLayout* bodyLayout = qobject_cast<QVBoxLayout*>(body->layout());
			if (bodyLayout == nullptr)
				return;
			QWidget* strip = new QWidget(body);
			strip->setObjectName(QStringLiteral("MatrixVstPortStrip"));
			QHBoxLayout* stripLayout = new QHBoxLayout(strip);
			stripLayout->setContentsMargins(0, 0, 0, 0);
			stripLayout->setSpacing(8);
			QLabel* inPort = new QLabel(QStringLiteral("> IN"), strip);
			inPort->setObjectName(QStringLiteral("MatrixVstPortLabel"));
			stripLayout->addWidget(inPort);
			stripLayout->addStretch(1);
			QLabel* device = new QLabel(QStringLiteral("EXTERNAL DEVICE"), strip);
			device->setObjectName(QStringLiteral("MatrixVstDeviceLabel"));
			stripLayout->addWidget(device);
			stripLayout->addStretch(1);
			QLabel* outPort = new QLabel(QStringLiteral("OUT >"), strip);
			outPort->setObjectName(QStringLiteral("MatrixVstPortLabel"));
			stripLayout->addWidget(outPort);
			bodyLayout->insertWidget(0, strip);
		}
	}

	// Painted board chrome: header band, 1px header rule, faint column grid,
	// status lamp, and the crosspoint hover (row band + coordinate-column
	// band). Drawn under the transparent header/body so children stay crisp.
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		if (info.type == QStringLiteral("spacer"))
			return;

		painter.setRenderHint(QPainter::Antialiasing, false);

		const QRect content = rect.adjusted(MatrixMetrics::railInset, 1, -1, -1);
		if (content.width() <= 0 || content.height() <= 0)
			return;
		const int headerHeight = qMin(tokens.rowHeight, content.height());
		const QRect headerBand(content.left(), content.top(), content.width(), headerHeight);

		// Header band fill (the header widget itself is transparent).
		painter.fillRect(headerBand, QColor(info.selected ? tokens.surfaceRaised : tokens.cardHover));

		// Faint column grid: the graph paper the board sits on.
		QColor gridColor(tokens.border);
		gridColor.setAlpha(info.enabled ? 60 : 30);
		painter.setPen(QPen(gridColor, 1));
		for (int x = content.left() + MatrixMetrics::gridPitch; x < content.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, content.top(), x, content.bottom());

		// 1px rule between the header cell and the body cell.
		if (content.height() > headerHeight)
		{
			painter.setPen(QPen(QColor(tokens.border), 1));
			painter.drawLine(content.left(), content.top() + headerHeight, content.right(), content.top() + headerHeight);
		}

		// Crosspoint hover: the row band and the coordinate-column band light
		// up; their intersection is the crosspoint.
		if (info.hovered && info.enabled)
		{
			QColor rowBand(tokens.accent);
			rowBand.setAlpha(22);
			painter.fillRect(headerBand, rowBand);
			const QRect columnBand(content.left(), content.top(),
				qMin(MatrixMetrics::coordinateBandWidth, content.width()), content.height());
			QColor columnColor(tokens.accent);
			columnColor.setAlpha(14);
			painter.fillRect(columnBand, columnColor);
			QColor crosspoint(tokens.accent);
			crosspoint.setAlpha(26);
			painter.fillRect(QRect(columnBand.left(), headerBand.top(), columnBand.width(), headerBand.height()), crosspoint);
		}

		// Status lamp in the left gutter: solid green = active, hollow amber =
		// bypassed (traffic-light semantics, never decorative).
		const QRect lampRect(content.left() + 1, content.top() + headerHeight / 2 - 3, 5, 5);
		if (info.enabled)
		{
			painter.fillRect(lampRect, QColor(tokens.success));
		}
		else
		{
			painter.setPen(QPen(QColor(tokens.warning), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(lampRect.adjusted(0, 0, -1, -1));
		}
	}
};

StudioSkin g_studio;
MinimalSkin g_minimal;
SoftSkin g_soft;
RackSkin g_rack;
MatrixSkin g_matrix;
}

namespace Skins
{
QList<ISkin*> all()
{
	return { &g_studio, &g_minimal, &g_soft, &g_rack, &g_matrix };
}

ISkin* byId(const QString& id)
{
	QString resolved = id;
	if (resolved == QStringLiteral("glassy"))
		resolved = QStringLiteral("studio");
	else if (resolved == QStringLiteral("industrial"))
		resolved = QStringLiteral("rack");

	for (ISkin* skin : all())
		if (skin->id() == resolved)
			return skin;
	return &g_studio;
}
}
