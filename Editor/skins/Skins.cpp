/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"

#include <QPainter>
#include <QtMath>

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

// ── Soft (macOS-like, rounded, soft shadows) ────────────────────────────────
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
		t.borderRadius = 18;
		t.rowHeight = 44;
		t.channelGroupIndent = 20;
		t.density = 2;
		t.channelGroupStyle = SkinTokens::SoftShadow;
		t.badgeStyle = SkinTokens::SoftPill;
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
};

// ── Rack (skeuomorphic 19" hardware) ────────────────────────────────────────
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
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 6;
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

// ── Matrix (signal matrix / instrument panel) ───────────────────────────────
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
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 10;
		t.rowHeight = 38;
		t.channelGroupIndent = 22;
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
		}
		finishTokens(t);
		return t;
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
