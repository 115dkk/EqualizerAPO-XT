/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"

#include <QAction>
#include <QComboBox>
#include <QDial>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

// Studio's S3 band-colour law maps BiQuad filter types onto hue families.
#include "filters/BiQuad.h"

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/RackChrome.h"
#include "Editor/skins/pickers/StudioFilterPicker.h"
#include "Editor/skins/pickers/MinimalFilterPicker.h"
#include "Editor/skins/pickers/SoftFilterPicker.h"
#include "Editor/skins/pickers/RackFilterPicker.h"
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

// S3 band-colour law (adversarial review round 1): the light a BiQuad row
// carries - knob arcs, type badge ink, signal lamp, hover/selected border
// glow - takes the row's band colour, one hue family per filter type. The
// glass itself stays neutral. Peaking and pure gain stay on the skin's base
// blue; shelves are mint, pass filters violet, notch/all-pass rose. Rows
// that carry no "studioBand" tag keep the neutral accent.
const char* const studioBandFamilies[] = { "peak", "shelf", "pass", "notch" };

QString studioBandHex(const QString& family, bool dark)
{
	if (family == QLatin1String("shelf"))
		return dark ? QStringLiteral("#44D7A4") : QStringLiteral("#0C9E72");
	if (family == QLatin1String("pass"))
		return dark ? QStringLiteral("#A66CFF") : QStringLiteral("#8A4DFF");
	if (family == QLatin1String("notch"))
		return dark ? QStringLiteral("#FF7FA8") : QStringLiteral("#DB4D7E");
	return dark ? QStringLiteral("#5B8CFF") : QStringLiteral("#2F6BFF");
}

QString studioBandFamilyForBiQuadType(int type)
{
	switch (type)
	{
	case BiQuad::LOW_SHELF:
	case BiQuad::HIGH_SHELF:
		return QStringLiteral("shelf");
	case BiQuad::LOW_PASS:
	case BiQuad::HIGH_PASS:
	case BiQuad::BAND_PASS:
		return QStringLiteral("pass");
	case BiQuad::NOTCH:
	case BiQuad::ALL_PASS:
		return QStringLiteral("notch");
	default:
		return QStringLiteral("peak");
	}
}

// Resolves the band colour a widget was tagged with (prepareCommandRow).
// The paint hooks receive no widget pointer, but painting always happens on
// the widget itself, so the painter's device is the tagged widget; untagged
// widgets fall back to the neutral accent.
QColor studioBandPaintColor(const QPainter& painter, const SkinTokens& tokens)
{
	QString hex = tokens.accent;
	if (painter.device() != nullptr && painter.device()->devType() == QInternal::Widget)
	{
		const QVariant family = static_cast<const QWidget*>(painter.device())->property("studioBand");
		if (family.isValid())
			hex = studioBandHex(family.toString(), studioIsDark(tokens));
	}
	return QColor(hex);
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

	// The "add filter" picker as a floating frosted-glass panel: painted
	// stage + glow, a prominent sunken-glass search field and a sectioned
	// list whose hover pools light under the cursor (StudioFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new StudioFilterPickerView(parent);
	}

	// The title bar is the topmost edge of the window's glass. The QSS keeps
	// the strip on the deep stage colour; this hook lays the light on it - a
	// whisper of reflection along the full top edge plus a faint accent-to-
	// violet arc caught on that edge, echoing the picker panel and the knob
	// arcs (accent2 stays at the spectrum's end, per the one-light rule).
	// Both are plain strokes: bloom first, then the core - glow is faked with
	// layered strokes, never effects.
	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		const bool dark = studioIsDark(tokens);
		painter.save();

		// The 1px lighter top edge of the glass formula, quieter than a
		// panel's reflection - the bar is the stage's edge, not a card.
		painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1.0),
			QColor(255, 255, 255, dark ? 30 : 235));

		painter.setRenderHint(QPainter::Antialiasing);
		const double span = rect.width() * 0.42;
		const double x0 = rect.left() + (rect.width() - span) / 2.0;
		const double y = rect.top() + 0.5;
		QLinearGradient bloom(x0, y, x0 + span, y);
		bloom.setColorAt(0.0, studioAlpha(tokens.accent, 0));
		bloom.setColorAt(0.35, studioAlpha(tokens.accent, dark ? 70 : 55));
		bloom.setColorAt(0.7, studioAlpha(tokens.accent2, dark ? 52 : 42));
		bloom.setColorAt(1.0, studioAlpha(tokens.accent2, 0));
		QPen bloomPen(QBrush(bloom), 4.0);
		bloomPen.setCapStyle(Qt::RoundCap);
		painter.setPen(bloomPen);
		painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));
		QLinearGradient core(x0, y, x0 + span, y);
		core.setColorAt(0.0, studioAlpha(tokens.accent, 0));
		core.setColorAt(0.35, studioAlpha(tokens.accent, dark ? 215 : 195));
		core.setColorAt(0.7, studioAlpha(tokens.accent2, dark ? 175 : 155));
		core.setColorAt(1.0, studioAlpha(tokens.accent2, 0));
		QPen corePen(QBrush(core), 1.5);
		corePen.setCapStyle(Qt::RoundCap);
		painter.setPen(corePen);
		painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));

		painter.restore();
	}

	// The toolbar is the top edge of the window's glass. The QSS sheets own
	// the strip itself (deep background, a 1px reflection along the bottom
	// edge, accent light pooling under hovered buttons, the Instant mode
	// lamp, sunken mono readouts); code only re-inks the file actions as
	// quiet ink - the muted colour lifted halfway toward the text ink - so
	// the icons rest behind the data until interaction lights the glass
	// under them, yet stay legible at menu size. Idempotent: re-tinting and
	// re-sizing converge on every skin/dark switch.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;

		const QColor text(tokens.text);
		const QColor muted(tokens.mutedText);
		const QColor ink((muted.red() + text.red()) / 2,
			(muted.green() + text.green()) / 2,
			(muted.blue() + text.blue()) / 2);
		toolBar->setIconSize(GUIHelper::scale(QSize(18, 18)));
		for (QAction* action : toolBar->actions())
		{
			if (action->objectName() == QStringLiteral("actionNew"))
				action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/file-new.svg"), ink, 18));
			else if (action->objectName() == QStringLiteral("actionOpen"))
				action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), ink, 18));
			else if (action->objectName() == QStringLiteral("actionSave"))
				action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save.svg"), ink, 18));
		}
	}

	// "The arc IS the value": no knob body, only a thin track circle, a
	// glowing arc from the reference point to the current value and a small
	// indicator dot. The arc wears the row's band colour (S3). Glow is a
	// luminance ladder faked with layered strokes: a faint outer stroke even
	// at rest, a one-step bloom on hover and full light while dragging (S4).
	// Bipolar (gain) knobs hang from a luminous 0 dB anchor at 12 o'clock and
	// grow left (cut) or right (boost); unipolar knobs grow from the track
	// start and carry no anchor (X3). The numeric readout fades in while
	// hovering or dragging; disabled knobs drop to reduced opacity.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing);

		// Centred square so the knob stays round in non-square hosts
		// (promoted legacy dials are 84x66, the card knob is 74x74).
		const QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
		const double side = qMin(inner.width(), inner.height());
		const QRectF track(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

		const double span = 270.0;
		const double start = 135.0;     // degrees clockwise from 3 o'clock
		const double ratio = qBound(0.0, state.ratio, 1.0);

		if (!state.enabled)
			painter.setOpacity(0.35);

		const QColor accent = studioBandPaintColor(painter, tokens);

		// Track: the full range geometry as a thin circle segment.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(tokens.border), 2.0, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(track, qRound(-start * 16), qRound(-span * 16));

		double arcFrom = start;
		double sweep = span * ratio;
		if (state.bipolar)
		{
			arcFrom = start + span / 2.0;  // 12 o'clock
			sweep = span * (ratio - 0.5);  // signed: cut grows left, boost right
		}

		// The luminance ladder (S4): rest keeps a faint outer stroke so the
		// arc visibly glows even untouched, hover blooms one full step and a
		// drag turns the light all the way up.
		const int halo = state.dragging ? 120 : (state.hovered ? 88 : 36);
		const struct { double width; int alpha; } layers[] = {
			{ 13.0, qMax(8, halo / 6) },
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

		// 0 dB anchor (X3): a luminous tick crossing the track at 12 o'clock,
		// drawn over the arc so the centre detent stays readable even at
		// small gains - bloom first, bright core on top (strokes, never
		// effects). At 0 dB the indicator dot sits right under it: the knob
		// visibly rests at its detent.
		if (state.bipolar)
		{
			const QPointF top(track.center().x(), track.top());
			QColor tickBloom = accent;
			tickBloom.setAlpha(110);
			painter.setPen(QPen(tickBloom, 3.5, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(top.x(), top.y() - 6.0), QPointF(top.x(), top.y() + 4.0));
			QColor tickCore(tokens.text);
			tickCore.setAlpha(235);
			painter.setPen(QPen(tickCore, 1.4, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(QPointF(top.x(), top.y() - 6.0), QPointF(top.x(), top.y() + 4.0));
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
	// lighter top edge (the reflection); paintCardChrome layers the caught
	// light on top (S1). Command types keep one silhouette but announce
	// themselves through the border treatment: DSP rows solid, Include rows
	// dashed (a reference to elsewhere), VST rows a vertical accent gradient
	// (the module radiates its own light). BiQuad rows hang their hover and
	// selection glow on the band colour they were tagged with (S3); the glass
	// itself stays neutral. Hover brightens; disabled rows lose the
	// reflection and most of their opacity.
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

		QString style = QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-top-color: %4; border-radius: 8px; }"
			" QFrame#FilterCardRow:hover { background: %5; border-color: %6; border-top-color: %7; }")
			.arg(background, borderStyle, borderBrush, topEdge, hoverBackground, hoverBorderBrush, topEdgeHover);

		// S3: a tagged BiQuad row's border light follows its band colour.
		// Attribute selectors outrank the base rules, and untagged rows can
		// never match them; keyboard focus keeps the neutral focus ring.
		if (info.type == QStringLiteral("biquad") && !info.focused)
		{
			for (const char* family : studioBandFamilies)
			{
				const QString band = studioBandHex(QLatin1String(family), dark);
				if (info.selected)
					style += QStringLiteral(" QFrame#FilterCardRow[studioBand=\"%1\"] { border-color: %2; border-top-color: %3; }")
						.arg(QLatin1String(family), studioRgba(band, 0.65), topEdge);
				style += QStringLiteral(" QFrame#FilterCardRow[studioBand=\"%1\"]:hover { border-color: %2; border-top-color: %3; }")
					.arg(QLatin1String(family), studioRgba(band, 0.45), topEdgeHover);
			}
		}
		return style;
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

	// Painted decoration on top of the QSS chrome - the layer that makes a
	// row read as glass instead of a flat dark rectangle (S1). Every enabled
	// row gets the pane treatment: a frost sheen settling down from the top
	// edge, a centre-bright reflection caught on that edge (dark mode; white
	// glass cannot get brighter) and a shade pooling at the bottom as the
	// pane's thickness. On top of the pane, DSP rows wear the signal lamp in
	// their band colour (S3) and VST rows a border-hugging halo, with hover
	// blooming one ladder step (S4). Include/comment/raw rows stay unlit
	// panes; disabled rows paint nothing - the light is off, the glass dead.
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		if (!info.enabled || info.type == QStringLiteral("spacer"))
			return;

		const bool dark = studioIsDark(tokens);
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing);

		// The pane: the glass surface treatment stays inside the border.
		const QRectF pane = QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0);
		QPainterPath panePath;
		panePath.addRoundedRect(pane, 7.0, 7.0);
		painter.setClipPath(panePath);

		if (dark)
		{
			// Frost sheen: room light caught in the upper glass.
			QLinearGradient sheen(pane.topLeft(), QPointF(pane.left(), pane.top() + pane.height() * 0.45));
			sheen.setColorAt(0.0, QColor(255, 255, 255, info.hovered ? 24 : 15));
			sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.fillPath(panePath, sheen);
		}

		// The pane's thickness: a shade pooling at the bottom edge. In light
		// mode this shade carries the whole glass impression.
		QLinearGradient depthShade(QPointF(pane.left(), pane.bottom() - pane.height() * 0.38), pane.bottomLeft());
		depthShade.setColorAt(0.0, QColor(0, 0, 0, 0));
		depthShade.setColorAt(1.0, dark ? QColor(0, 0, 0, 52) : QColor(24, 32, 51, 26));
		painter.fillPath(panePath, depthShade);

		if (dark)
		{
			// Centre-bright reflection just under the border's top edge - the
			// title bar's whisper of light at card scale.
			const double y = pane.top() + 0.5;
			QLinearGradient reflection(pane.left(), y, pane.right(), y);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, info.hovered ? 84 : 56));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(pane.left() + 6.0, y), QPointF(pane.right() - 6.0, y));
		}

		painter.setClipping(false);

		if (info.type == QStringLiteral("comment") || info.type == QStringLiteral("text")
			|| info.type == QStringLiteral("include"))
		{
			// Unlit panes: the glass surface only, no lamp. Include points
			// elsewhere; comments and raw text carry no signal.
			painter.restore();
			return;
		}

		if (info.type == QStringLiteral("vst"))
		{
			// Two strokes hugging the border fake an outer glow without
			// effects; hover turns the light up a full step (S4).
			const QRectF edge = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(studioAlpha(tokens.accent, info.hovered ? 150 : 80), 1.0));
			painter.drawRoundedRect(edge, 8.0, 8.0);
			painter.setPen(QPen(studioAlpha(tokens.accent, info.hovered ? 80 : 36), 3.0));
			painter.drawRoundedRect(edge.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);
		}
		else
		{
			// Signal lamp in the row's band colour (S3), blooming on hover.
			const QColor light = studioBandPaintColor(painter, tokens);
			const double segment = 18.0;
			const double y0 = rect.top() + (tokens.rowHeight - segment) / 2.0;
			QColor mid = light;
			mid.setAlpha(info.hovered ? 255 : 195);
			QColor fade = light;
			fade.setAlpha(0);
			QLinearGradient lamp(0, y0, 0, y0 + segment);
			lamp.setColorAt(0.0, fade);
			lamp.setColorAt(0.5, mid);
			lamp.setColorAt(1.0, fade);
			painter.setPen(Qt::NoPen);
			// Bloom first (wider, fainter), then the lamp core on the edge.
			QColor bloomMid = light;
			bloomMid.setAlpha(info.hovered ? 90 : 46);
			QLinearGradient bloom(0, y0 - 3.0, 0, y0 + segment + 3.0);
			bloom.setColorAt(0.0, fade);
			bloom.setColorAt(0.5, bloomMid);
			bloom.setColorAt(1.0, fade);
			painter.fillRect(QRectF(rect.left(), y0 - 3.0, 4.0, segment + 6.0), bloom);
			painter.fillRect(QRectF(rect.left(), y0, 2.0, segment), lamp);
		}

		painter.restore();
	}

	// The type badge is a lit glass chip (S3): translucent fill with the ink
	// and border in the row's light colour. BiQuad rows resolve their family
	// through the studioBand property the prepareCommandRow hook tagged them
	// with; other commands keep their model colour as quiet ink, not a second
	// light source. Disabled rows switch the chip off.
	QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& tokens) const override
	{
		const bool dark = studioIsDark(tokens);
		if (!info.enabled)
		{
			return QStringLiteral("QLabel#FilterTypeBadge { color: %1; background-color: transparent; border: 1px solid %2; }")
				.arg(studioRgba(tokens.mutedText, 0.65), studioRgba(tokens.border, 0.55));
		}

		const QString baseInk = info.type == QStringLiteral("biquad") ? tokens.accent : typeColor;
		QString style = QStringLiteral("QLabel#FilterTypeBadge { color: %1; background-color: %2; border: 1px solid %3; }")
			.arg(baseInk, studioRgba(baseInk, dark ? 0.15 : 0.10), studioRgba(baseInk, dark ? 0.42 : 0.45));
		if (info.type == QStringLiteral("biquad"))
		{
			for (const char* family : studioBandFamilies)
			{
				const QString band = studioBandHex(QLatin1String(family), dark);
				style += QStringLiteral(" QLabel#FilterTypeBadge[studioBand=\"%1\"] { color: %2; background-color: %3; border: 1px solid %4; }")
					.arg(QLatin1String(family), band, studioRgba(band, dark ? 0.15 : 0.10), studioRgba(band, dark ? 0.42 : 0.45));
			}
		}
		return style;
	}

	// Tags BiQuad rows with their band family (S3) so the QSS attribute
	// selectors (frame hover/selected border, type badge chip) and the paint
	// hooks (knob arcs, signal lamp) all light the row in one colour. The tag
	// follows the type selector live; repolishing re-evaluates the same rules
	// cardFrameStyle/typeBadgeStyle returned at construction.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		if (body == nullptr || info.type != QStringLiteral("biquad"))
			return;

		QComboBox* typeCombo = nullptr;
		for (QComboBox* combo : body->findChildren<QComboBox*>())
		{
			if (combo->property("filterSelector").toBool())
			{
				typeCombo = combo;
				break;
			}
		}
		if (typeCombo == nullptr)
			return;

		const auto applyBand = [card, header, body, typeCombo]() {
			const QString family = studioBandFamilyForBiQuadType(typeCombo->currentData().toInt());
			const auto tag = [&family](QWidget* widget) {
				if (widget == nullptr || widget->property("studioBand").toString() == family)
					return;
				widget->setProperty("studioBand", family);
				widget->style()->unpolish(widget);
				widget->style()->polish(widget);
				widget->update();
			};
			tag(card);
			if (header != nullptr)
				tag(header->findChild<QLabel*>(QStringLiteral("FilterTypeBadge")));
			// AudioKnob extends QDial; the knob paint hook reads the tag off
			// the painter's device.
			for (QDial* knob : body->findChildren<QDial*>())
				tag(knob);
		};
		applyBand();
		QObject::connect(typeCombo, &QComboBox::currentIndexChanged, typeCombo, applyBand);
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
			// S2 re-derivation: the light tokens keep the accent saturation
			// but the borders sit two steps deeper than the airy panels, so
			// knob tracks, toggles and input edges stay legible on white
			// glass; muted ink deepens one step with them.
			t.background = QStringLiteral("#EEF2F8");
			t.surface = QStringLiteral("#F8FAFE");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#F3F6FC");
			t.cardSelected = QStringLiteral("#DDE8FF");
			t.text = QStringLiteral("#182033");
			t.mutedText = QStringLiteral("#5D6A84");
			t.border = QStringLiteral("#BCC8DE");
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

// ANNEX K minimal: "the number is the control; the knob is confirmation"
// (N2). The figure is the brightest ink in the row - painted here when the
// widget supplies valueText, living in the adjacent ValueScrubBox (promoted
// by precision_*.qss) for the row dials, which supply none. The knob itself
// is a hairline instrument: a 1px 270-degree range arc, a travelled arc in
// text ink and a radial cursor tick at the value angle - no filled disc, no
// hub. Unipolar dials measure travel from the range start; bipolar dials
// measure a deviation arc from a fixed 12 o'clock detent tick (boost
// clockwise, cut counter-clockwise), so the two kinds part at a glance (X3)
// and 0 dB reads as "cursor on the detent, no deviation". Monochrome until
// dragged; dragging turns the travelled ink accent (active-state law).
void paintMinimalKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor hairline(tokens.border);
	const QColor secondary(tokens.mutedText);
	const QColor active(tokens.accent);
	// The promoted figure sits one brightness step above body text: white on
	// the dark console, full black on the light paper. Mode is read off the
	// background's value because SkinTokens carries no dark flag.
	const bool darkMode = QColor(tokens.background).lightness() < 128;
	const QColor promoted(!state.enabled ? secondary
		: (darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0)));
	const QColor travelled = !state.enabled ? secondary
		: (state.dragging ? active : QColor(tokens.text));

	const bool hasNumber = !state.valueText.isEmpty();
	const double arcRadius = hasNumber ? 9.0 : 12.0;

	QFont numberFont(tokens.monoFontFamily);
	numberFont.setBold(true);
	numberFont.setPointSizeF(9.0);

	QPointF arcCenter;
	QRectF numberRect;
	if (hasNumber)
	{
		// Number left (primary), confirmation arc beside it; the pair is
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
		arcCenter = QPointF(left + textWidth + gap + arcRadius, QRectF(rect).center().y());
	}
	else
	{
		// Arc only; keep a constant bottom strip free for the hover/drag
		// readout so the instrument does not jump when the readout appears.
		arcCenter = QPointF(QRectF(rect).center().x(), rect.top() + (rect.height() - 14.0) / 2.0);
	}

	// Hairline range arc: the full 270-degree travel, 1px, open across the
	// bottom dead zone like every knob in the product.
	const QRectF arcRect(arcCenter.x() - arcRadius, arcCenter.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(arcRect, -135 * 16, -270 * 16);

	if (state.bipolar)
	{
		// Fixed detent tick at 12 o'clock and a 1px deviation arc measured
		// from it: boost grows clockwise, cut counter-clockwise. On the
		// detent the deviation vanishes and only the tick remains - the
		// honest "0 dB".
		painter.setPen(QPen(secondary, 1));
		painter.drawLine(minimalPointOnArc(arcCenter, arcRadius - 2.5, -270.0),
			minimalPointOnArc(arcCenter, arcRadius + 2.5, -270.0));
		const double deviationDegrees = 270.0 * (state.ratio - 0.5);
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -270 * 16, -qRound(deviationDegrees * 16.0));
	}
	else
	{
		// Unipolar: the travelled range fills from the arc's start. No detent
		// tick, no centre origin - the two kinds cannot be confused.
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -135 * 16, -qRound(270.0 * state.ratio * 16.0));
	}

	// Radial cursor tick crossing the range arc at the value angle.
	const double valueDegrees = -(135.0 + 270.0 * state.ratio);
	painter.setPen(QPen(travelled, 1));
	painter.drawLine(minimalPointOnArc(arcCenter, arcRadius - 3.0, valueDegrees),
		minimalPointOnArc(arcCenter, arcRadius + 3.0, valueDegrees));

	if (hasNumber)
	{
		painter.setFont(numberFont);
		painter.setPen((state.enabled && state.dragging) ? active : promoted);
		painter.drawText(numberRect, Qt::AlignVCenter | Qt::AlignLeft, state.valueText);
	}
	else if (state.enabled && (state.hovered || state.dragging))
	{
		// No supplied value text: show the dial position derived from ratio.
		// The real value sits in the adjacent scrub box, so a percentage is
		// the only honest readout for log-scaled legacy dials.
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

// AR2 - dress the Include/VST reference card (the shared ReferenceCard widget)
// in the terminal's grammar. The card carries the same information hierarchy in
// every skin (name primary, directory secondary, missing = a state transition,
// Locate = the recovery entry point); minimal translates it without colour.
// The broken-reference state is the inverted [MISSING] token (the picker's
// bluntest cursor - the foreground/background swap - borrowed as a status
// token), the portability hazard is a bracketed [ABS] text token, the format
// is a 1px OutlineOnly box, and the recovery action is the imperative command
// word LOCATE. The body editors consult this hook at construction, before their
// first setState, so the styles and badge text set here survive: setState only
// flips dynamic properties (clickable / refMissing / severity) and the
// format-badge text, all of which the selectors here account for. The SVG icon
// is hidden because an ASCII-glyph skin already leads the line with its >> / []
// type glyph, so a second pictogram would be redundant decor (rule 5/6).
//
// ReferenceCard sets its child labels' stylesheets inline at construction, and
// an inline stylesheet beats the app sheet, so precision_*.qss cannot reach
// these labels; re-setting the inline stylesheet here is the honest way to
// re-dress them.
void styleMinimalReferenceCard(QWidget* body, const QString& type, const SkinTokens& tokens)
{
	if (body == nullptr)
		return;
	const bool dark = QColor(tokens.background).lightness() < 128;
	const QString brightInk = dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
	const QString cap = type == QStringLiteral("vst") ? QStringLiteral("Vst") : QStringLiteral("Include");

	// The SVG icon is foreign here; the line head's glyph already names the type.
	if (QLabel* icon = body->findChild<QLabel*>(cap + QStringLiteral("RefIcon")))
		icon->setVisible(false);

	// Primary name: body ink at rest, the brightest ink plus a hyperlink
	// underline when it is the open/jump affordance (accent is reserved for
	// active controls, so the clickable cue is the underline, not a colour). A
	// broken reference keeps the file name in body ink - the [MISSING] token,
	// not a colour, carries the state.
	if (QLabel* name = body->findChild<QLabel*>(cap + QStringLiteral("RefName")))
		name->setStyleSheet(QStringLiteral(
			"QLabel { color:%1; font-weight:700; background:transparent; }"
			"QLabel[clickable=\"true\"] { color:%2; text-decoration:underline; }"
			"QLabel[refMissing=\"true\"] { color:%1; }")
			.arg(tokens.text, brightInk));

	// Secondary directory: muted monospace; hierarchy is brightness, not a new
	// colour. Middle-elision (X-6) stays, the full path lives in the tooltip.
	if (QLabel* dir = body->findChild<QLabel*>(cap + QStringLiteral("RefDir")))
		dir->setStyleSheet(QStringLiteral(
			"color:%1; background:transparent; font-family:\"%2\"; font-size:9pt;")
			.arg(tokens.mutedText, tokens.monoFontFamily));

	// Status line: severity is read as ink brightness, never a traffic-light
	// colour (that grammar belongs to the neighbouring matrix skin). A critical
	// line climbs to body ink; a warning stays muted.
	if (QLabel* status = body->findChild<QLabel*>(cap + QStringLiteral("RefStatus")))
		status->setStyleSheet(QStringLiteral(
			"QLabel { color:%1; background:transparent; font-size:9pt; }"
			"QLabel[severity=\"warning\"] { color:%1; }"
			"QLabel[severity=\"critical\"] { color:%2; }")
			.arg(tokens.mutedText, tokens.text));

	// Format badge (VST2/VST3): the OutlineOnly token - a 1px hairline square
	// box, no fill, uppercase mono. setState rewrites its text so it cannot wear
	// brackets; the box is the bracket's graphic equivalent.
	if (QLabel* fmt = body->findChild<QLabel*>(cap + QStringLiteral("FormatBadge")))
		fmt->setStyleSheet(QStringLiteral(
			"QLabel { color:%1; background:transparent; border:1px solid %2; border-radius:0;"
			" padding:0 4px; font-size:8pt; font-weight:700; letter-spacing:1px; }")
			.arg(tokens.mutedText, tokens.border));

	// Absolute-path hazard: a bracketed text token. Brackets are the ASCII form
	// of the OutlineOnly badge, so it needs no box and no warning colour - the
	// [ABS] token itself says "external reference".
	if (QLabel* abs = body->findChild<QLabel*>(cap + QStringLiteral("RefAbsBadge")))
	{
		abs->setText(QStringLiteral("[ABS]"));
		abs->setStyleSheet(QStringLiteral(
			"QLabel { color:%1; background:transparent; border:0; padding:0 2px;"
			" font-weight:700; letter-spacing:1px; }")
			.arg(tokens.mutedText));
	}

	// Missing badge: the loudest state token. minimal cannot say "missing" in
	// danger red (colour-as-status is matrix's grammar), so it borrows the
	// picker's bluntest cursor - the inverted block - and prints [MISSING]
	// foreground/background swapped. Square, monochrome, unmistakable (X-3).
	if (QLabel* missing = body->findChild<QLabel*>(QStringLiteral("RefMissingBadge")))
	{
		missing->setText(QStringLiteral("[MISSING]"));
		missing->setStyleSheet(QStringLiteral(
			"QLabel { color:%1; background:%2; border:0; border-radius:0; padding:0 5px;"
			" font-weight:700; letter-spacing:1px; }")
			.arg(tokens.background, tokens.text));
	}

	// Recovery action: the imperative command word, no icon (a text instrument
	// names its commands - rule 5). precision_*.qss boxes RefLocateAction as a
	// hairline and walks its states up the value ladder.
	if (QToolButton* locate = body->findChild<QToolButton*>(QStringLiteral("RefLocateAction")))
	{
		locate->setText(QStringLiteral("LOCATE"));
		locate->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		// The add-filter dropdown as a numbered terminal index; see
		// MinimalFilterPicker.h for the design.
		return new MinimalFilterPickerView(parent);
	}
	// The toolbar is the terminal's command line: all type and one hairline.
	// The neutral default keeps the shared stroke icons on the actions so the
	// File menu (which shares the QActions) stays modern; the toolbar buttons
	// themselves drop the pictures and show the command words instead -
	// precision_*.qss uppercases and letter-spaces them and walks their states
	// up the value ladder (hover = one background step, pressed = the
	// inverted block from the picker's cursor grammar). Both calls set
	// absolute state, so re-running on every skin/dark switch is idempotent.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;
		ISkin::styleMainToolbar(toolBar, tokens);
		toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
		// Leading type glyph at the line head. Only modern card rows carry a
		// header here; the Include/VST body editors consult the hook with
		// header == nullptr and a live body, where their shared reference card
		// gets the terminal's grammar; the frozen legacy rows pass body == nullptr
		// and stay untouched.
		if (header == nullptr)
		{
			if (body != nullptr && (info.type == QStringLiteral("include") || info.type == QStringLiteral("vst")))
				styleMinimalReferenceCard(body, info.type, SkinManager::instance()->tokens());
			return;
		}
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

// The hooks receive tokens but not the mode flag; like studio, the background
// luminance is an unambiguous proxy (soft's dark background is deep graphite).
bool softIsDark(const SkinTokens& tokens)
{
	return QColor(tokens.background).lightness() < 128;
}

// AR1 F2: the picker's pastel multi-hue grammar, normalised for row chrome.
// The hue comes from an existing colour (the command type's descriptor
// colour), and only saturation/lightness are re-seated on the pastel shelf -
// the same "no new palette, derive from what is already there" rule that
// softMix implements for elevation. Greyish type colours keep their low
// saturation instead of being inflated into a fake hue.
QColor softPastelize(const QColor& base, bool dark)
{
	const double hue = base.hslHueF() < 0.0 ? 215.0 / 360.0 : base.hslHueF();
	const double saturation = qMin(base.hslSaturationF(), dark ? 0.50 : 0.55);
	return QColor::fromHslF(hue, saturation, dark ? 0.62 : 0.60);
}

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

	// The picker is this skin's consumer-settings moment: a rounded menu card
	// with a pill search field, pastel category tiles and stadium row
	// highlights (skins/pickers/SoftFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new SoftFilterPickerView(parent);
	}

	// Window chrome: deliberately NO paintTitleBarChrome override. The
	// constitutional tiebreaker ("when in doubt, remove the element and add
	// whitespace") answers painted caption decoration directly - the calm app
	// header is already complete in the QSS sheets: the surface one value
	// step off the window, a friendly-weight title in full ink, caption
	// buttons resting as soft rounded squares whose hover lifts one value
	// step on a stadium highlight, and a close button that warms with the
	// dirty-badge amber instead of alarming red. Anything painted on top
	// (screws, glows, grids) belongs to the neighbours' vocabularies and
	// would only make the header more anxious.

	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.accent = QStringLiteral("#3B82F6");
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		// Constitution: cards 14px (clearly rounder than studio's 8), generous
		// line spacing. The tallest row of the five skins; whitespace is the
		// hierarchy device.
		t.borderRadius = 14;
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
			// AR1 F2: warm graphite, not navy. The old #171923..#3A4056 ramp
			// shared studio's cold blue cast, so soft-dark photographed as a
			// studio clone; the dark identity now leans warm (hue ~38, low
			// saturation) while the light mode keeps its cream. Same two-step
			// elevation ladder, different temperature.
			t.background = QStringLiteral("#1C1A17");
			t.surface = QStringLiteral("#262320");
			t.card = QStringLiteral("#2F2B26");
			t.cardHover = QStringLiteral("#38332D");
			t.cardSelected = QStringLiteral("#33415C");
			t.text = QStringLiteral("#F4F1EA");
			t.mutedText = QStringLiteral("#B3AB9D");
			t.border = QStringLiteral("#423D34");
			t.graph = QStringLiteral("#181613");
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

	// AR1 F2: the row's type badge wears the picker's pastel grammar instead
	// of the shared saturated pill, so the multi-hue "consumer settings"
	// identity survives into the command list (and into dark mode, where the
	// old badge was the only colour that separated soft from studio). The ink
	// is a deep warm neutral on the pastel chip - white text on a pastel is
	// exactly the kind of low-contrast anxiety this skin removes. A sleeping
	// (commented-out) row sinks its chip toward the window background.
	QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& t) const override
	{
		const bool dark = softIsDark(t);
		const QColor pastel = softPastelize(QColor(typeColor), dark);
		if (!info.enabled)
		{
			const QColor sleeping = softMix(pastel, QColor(t.background), 0.62);
			return QStringLiteral("color:%1; border-color:transparent; background-color:%2;")
				.arg(t.mutedText, sleeping.name());
		}
		return QStringLiteral("color:#2B251D; border-color:transparent; background-color:%1;")
			.arg(pastel.name());
	}

	// Annex K, soft: "a handle you cannot fumble". The largest knob of the
	// five skins. Two-step elevation body, rounded dot indicator (no sharp
	// line), value in a rounded badge below. AR1 F3/X3: the full travel is an
	// always-visible pastel track ring (accent mixed far toward the card), and
	// bipolar knobs differ from unipolar ones at rest, not only when turned -
	// their track splits at 12 o'clock into an accent2 cut half and an accent
	// boost half, with a soft detent tick crossing the ring at the 0 dB
	// centre. The value arc grows from that detent (boost right in accent,
	// cut left in accent2); unipolar arcs grow from the minimum.
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

		// Always-visible pastel track ring (F3). Unipolar travel wears one
		// accent pastel; a bipolar knob splits at the 12 o'clock detent into
		// an accent2 cut half and an accent boost half, so gain reads as
		// two-sided even while it rests at 0 dB.
		const double centerDegrees = startDegrees + spanDegrees / 2.0;
		if (state.enabled && state.bipolar)
		{
			painter.setPen(QPen(softMix(QColor(tokens.accent2), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees / 2.0 * 16.0));
			painter.setPen(QPen(softMix(QColor(tokens.accent), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-spanDegrees / 2.0 * 16.0));
		}
		else
		{
			const QColor trackColor = state.enabled ? softMix(QColor(tokens.accent), card, 0.80) : softAlpha(border, 110);
			painter.setPen(QPen(trackColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);
		}

		// Pastel value arc (accent softened one step toward the card colour).
		if (state.enabled)
		{
			const bool cutSide = state.bipolar && ratio < 0.5;
			const QColor valueColor = softMix(QColor(cutSide ? tokens.accent2 : tokens.accent), card, 0.25);
			painter.setPen(QPen(valueColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
			if (state.bipolar)
			{
				painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-(endDegrees - centerDegrees) * 16.0));
			}
			else
			{
				painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees * ratio * 16.0));
			}
		}

		// X3: the 0 dB detent is a soft rounded tick crossing the track ring
		// at 12 o'clock, painted over the value arc so the neutral point stays
		// marked however far the knob is turned. Only bipolar (gain) knobs
		// carry it - one more way the two knob kinds differ at a glance.
		if (state.bipolar)
		{
			const QPointF arcCenter = arcRect.center();
			const double trackRadius = arcRect.width() / 2.0;
			painter.setPen(QPen(softAlpha(QColor(tokens.text), state.enabled ? 200 : 90), 2.5, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(arcCenter.x(), arcCenter.y() - trackRadius - arcWidth / 2.0 + 0.5),
				QPointF(arcCenter.x(), arcCenter.y() - trackRadius + arcWidth / 2.0 - 0.5));
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

		// Rounded dot indicator instead of a sharp line; it grows slightly on
		// hover and again while dragging, the calmest possible "I am held" cue.
		// AR1 F3: the dot is larger than the pre-review 3.5px minimum so the
		// position reads from across the row, and on a bipolar knob it takes
		// the colour of the side it sits on (accent boost, accent2 cut).
		double dotRadius = qMax(4.5, side * 0.085);
		if (state.dragging)
			dotRadius += 1.0;
		else if (state.hovered)
			dotRadius += 0.5;
		const double dotTrack = faceRect.width() / 2.0 - dotRadius - 2.5;
		const double radians = qDegreesToRadians(-endDegrees);
		const QPointF dotPos(faceRect.center().x() + qCos(radians) * dotTrack,
			faceRect.center().y() - qSin(radians) * dotTrack);
		painter.setPen(Qt::NoPen);
		const QColor dotColor(state.bipolar && ratio < 0.5 ? tokens.accent2 : tokens.accent);
		painter.setBrush(state.enabled ? dotColor : softAlpha(muted, 120));
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

	// The toolbar is this skin's calm header band; the QSS sheets carry the
	// band, the toggle, the pills and the stadium combos. The hook's share is
	// the one thing QSS cannot express: the three file actions wear
	// iOS-Settings-style rounded-square colour tiles - pastels mixed from
	// existing tokens (accent blue for New, warning amber for the folder,
	// success green for Save) under the shared stroke glyph, the same tile
	// recipe as the picker's category tiles. Re-running the hook only calls
	// setters, so skin/dark switches stay idempotent.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;

		toolBar->setIconSize(GUIHelper::scale(QSize(22, 22)));
		const QColor card(tokens.card);
		for (QAction* action : toolBar->actions())
		{
			if (action->objectName() == QStringLiteral("actionNew"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/file-new.svg"), softMix(QColor(tokens.accent), card, 0.15)));
			else if (action->objectName() == QStringLiteral("actionOpen"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/folder-open.svg"), softMix(QColor(tokens.warning), card, 0.15)));
			else if (action->objectName() == QStringLiteral("actionSave"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/save.svg"), softMix(QColor(tokens.success), card, 0.15)));
		}
	}

private:
	// One icon, several pre-rendered sizes (16px for the File menu rows up to
	// the 22px toolbar size and beyond), so Qt never stretches a tile. The
	// tile matches SoftFilterPicker's category tiles: a rounded square at 32%
	// corner radius with the glyph inked in the picker's near-white literal.
	static QIcon softTileIcon(const QString& resource, const QColor& tile)
	{
		QIcon icon;
		// 44/64 keep the tile crisp on 2x displays (22/32 logical at DPR 2).
		for (const int logical : { 16, 18, 20, 22, 24, 32, 44, 64 })
		{
			const int side = GUIHelper::scale(double(logical));
			QPixmap pixmap(side, side);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(tile);
			painter.drawRoundedRect(QRectF(0, 0, side, side), side * 0.32, side * 0.32);
			const int glyphSide = qMax(10, qRound(logical * 0.66));
			const QPixmap glyph = GUIHelper::tintedIcon(resource, QColor(QStringLiteral("#FAFAFC")), glyphSide)
				.pixmap(GUIHelper::scale(QSize(glyphSide, glyphSide)));
			// Centre by the glyph's LOGICAL size: on high-DPR displays
			// QIcon::pixmap returns a pixmap whose width() is physical pixels
			// (dpr baked in), and drawPixmap honors the dpr - centring by
			// width() shoved the glyph toward the top-left at 200% scale.
			const QSizeF glyphLogical = glyph.deviceIndependentSize();
			painter.drawPixmap(QPointF((side - glyphLogical.width()) / 2.0,
				(side - glyphLogical.height()) / 2.0), glyph);
			painter.end();
			icon.addPixmap(pixmap);
		}
		return icon;
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
		// the hover state shines through them). The resting border is the dark
		// seam of the rack opening rather than the token border, so stacked
		// units separate physically (R3); focus and selection keep their
		// signal colours.
		const bool dark = QColor(tokens.background).lightness() < 128;
		const QString seam = dark ? QStringLiteral("#060809") : QStringLiteral("#8F8268");
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : seam);
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

	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		// The caption strip is the unit's top panel: brushed sheen, machined
		// edges, the caption-ear groove and two rail screws (RackChrome). QSS
		// prints the model designation and dresses the caption buttons as
		// machined caps.
		RackChrome::paintTitleBarChrome(painter, rect, tokens);
	}

	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		// The module library browser: a brushed 1U faceplate with engraved
		// section plates, LED-lit slots and an LCD search strip.
		return new RackFilterPickerView(parent);
	}

	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		// The shared stroke icons first (tinted with the panel's warm ink),
		// then the master-rail chrome: RackChrome mounts a painted overlay
		// (brushed strip, machined edges, end screws, engraved series
		// marking, instant-mode power LED) under the toolbar's controls. The
		// QSS dresses the controls themselves as transport buttons and an
		// LCD save-state well.
		ISkin::styleMainToolbar(toolBar, tokens);
		RackChrome::styleMainToolbar(toolBar, tokens);
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

// Bus designation of a command type (M2): the row coordinate speaks the same
// letter-plus-number grammar as the picker's board coordinates (A1..G8), with
// the letter mirroring the mono type code the badge cell already shows
// (BQUAD -> B, INC -> I, VST -> V, ...). The letter is a designation, not an
// identifier - two types may share one, exactly like two flights share a
// carrier letter; uniqueness stays with the line number, which never
// renumbers while the board is scanned.
QString matrixBusLetter(const QString& type)
{
	if (type == QStringLiteral("biquad"))
		return QStringLiteral("B");
	if (type == QStringLiteral("preamp"))
		return QStringLiteral("P");
	if (type == QStringLiteral("delay") || type == QStringLiteral("device"))
		return QStringLiteral("D");
	if (type == QStringLiteral("graphiceq"))
		return QStringLiteral("G");
	if (type == QStringLiteral("copy") || type == QStringLiteral("channel") || type == QStringLiteral("convolution"))
		return QStringLiteral("C");
	if (type == QStringLiteral("include"))
		return QStringLiteral("I");
	if (type == QStringLiteral("vst"))
		return QStringLiteral("V");
	if (type == QStringLiteral("stage"))
		return QStringLiteral("S");
	if (type == QStringLiteral("loudness"))
		return QStringLiteral("L");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	// Unrecognized raw lines: a remark entry on the board.
	return QStringLiteral("R");
}

// Per-row caption strip (M2): the picker footer's grammar imported into the
// card. A sunken board line fixed under the card body echoes the row's raw
// spec ("> Filter: ON PK ...") next to the row's board coordinate, exactly
// like the picker footer echoes the line an engaged coordinate would insert.
// At rest the readout idles in muted ink; while the row crosspoint is hovered
// the echo lights - marker and coordinate in accent, spec in full ink. The
// strip needs no event machinery: the frame's :hover QSS rule already forces
// a frame repaint on enter/leave (the same trigger the painted column band
// uses), which redraws this child, and the gallery's WA_UnderMouse hover
// equivalent drives it the same way. It replaces the shared raw-preview
// strip for this skin (tokens().showRawPreview = false), so the row spends
// the same vertical budget on a line that follows the board's grammar.
class MatrixRowCaption : public QWidget
{
public:
	MatrixRowCaption(QWidget* card, QLabel* specSource, QLabel* coordinateSource)
		: QWidget(card), specSource(specSource), coordinateSource(coordinateSource)
	{
		setObjectName(QStringLiteral("MatrixRowCaption"));
		// The strip is a readout, never a control; clicks fall through.
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFixedHeight(18);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, false);

		QWidget* card = parentWidget();
		// The dynamic property is kept current by FilterCardRow's restyles;
		// before the first restyle it is simply unset, which reads enabled.
		const QVariant enabledProperty = card != nullptr ? card->property("filterEnabled") : QVariant();
		const bool enabled = !enabledProperty.isValid() || enabledProperty.toBool();
		const bool lit = enabled && card != nullptr && card->underMouse();

		// Sunken board line under a 1px top rule - the picker footer's
		// construction.
		painter.fillRect(rect(), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(0, 0, width() - 1, 0);

		QFont mono(tokens.monoFontFamily);
		mono.setPointSizeF(7.5);
		painter.setFont(mono);
		const QFontMetrics metrics(mono);

		QColor idleInk(tokens.mutedText);
		if (!enabled)
			idleInk.setAlpha(120);
		const QColor accent(tokens.accent);
		const int pad = 10;

		// Board coordinate readout on the right.
		const QString coordinate = coordinateSource != nullptr ? coordinateSource->text() : QString();
		const int coordinateWidth = metrics.horizontalAdvance(coordinate);
		painter.setPen(lit ? accent : idleInk);
		painter.drawText(QRect(width() - pad - coordinateWidth, 0, coordinateWidth, height()),
			Qt::AlignVCenter | Qt::AlignLeft, coordinate);

		// "> <raw line>" spec echo. The raw-preview label keeps its text
		// current on every model rebuild even while hidden, so it doubles as
		// the live source of the row's raw spec.
		QString spec = specSource != nullptr ? specSource->text() : QString();
		if (spec.startsWith(QStringLiteral("Raw")))
			spec = spec.mid(3).trimmed();
		const QString marker = QStringLiteral("> ");
		painter.drawText(QRect(pad, 0, width(), height()), Qt::AlignVCenter | Qt::AlignLeft, marker);
		const int specX = pad + metrics.horizontalAdvance(marker);
		const int specAvail = width() - pad - coordinateWidth - 12 - specX;
		painter.setPen(lit ? QColor(tokens.text) : idleInk);
		painter.drawText(QRect(specX, 0, qMax(0, specAvail), height()), Qt::AlignVCenter | Qt::AlignLeft,
			metrics.elidedText(spec, Qt::ElideRight, qMax(0, specAvail)));
	}

private:
	QLabel* specSource;
	QLabel* coordinateSource;
};

// Painted chrome layers for the main toolbar (the board's header strip).
// QSS cannot draw the 24px column grid or the status lamp, so the matrix
// toolbar hook parents two transparent, mouse-transparent widgets to the
// toolbar: UnderCells (lowered below every cell) paints the column grid,
// the doubled header rule and the sunken fill of the status readout cell;
// OverCells (raised above the cells) paints the DirtyStatusBadge lamp on
// top of that readout. Instances are found again by object name on every
// hook run (this file has no moc, so findChild by class is unavailable),
// and painting self-suspends while another skin is active because the real
// MainWindow toolbar keeps its children across runtime skin switches.
class MatrixToolbarBoard : public QWidget
{
public:
	enum Layer { UnderCells, OverCells };

	MatrixToolbarBoard(QToolBar* toolBar, Layer boardLayer)
		: QWidget(toolBar), layer(boardLayer)
	{
		setObjectName(layer == UnderCells
			? QStringLiteral("MatrixToolbarBoardUnder")
			: QStringLiteral("MatrixToolbarBoardOver"));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		toolBar->installEventFilter(this);
		if (layer == OverCells)
		{
			// The lamp must follow the badge's dirty-state restyles and the
			// layout moving the cell around.
			if (QWidget* badge = toolBar->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge")))
				badge->installEventFilter(this);
		}
		setGeometry(toolBar->rect());
		if (layer == UnderCells)
			lower();
		else
			raise();
		show();
	}

	void setBoardTokens(const SkinTokens& tokens)
	{
		ruleColor = QColor(tokens.border);
		sunkenColor = QColor(tokens.surfaceSunken);
		savedColor = QColor(tokens.success);
		modifiedColor = QColor(tokens.warning);
		// The hook carries no mode flag; infer it from the strip's surface
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		gridAlpha = QColor(tokens.surface).lightness() < 128 ? 55 : 90;
		update();
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget())
		{
			if (event->type() == QEvent::Resize)
				setGeometry(parentWidget()->rect());
		}
		else if (event->type() == QEvent::Paint || event->type() == QEvent::Move
			|| event->type() == QEvent::Resize || event->type() == QEvent::Show
			|| event->type() == QEvent::Hide)
		{
			update();
		}
		return QWidget::eventFilter(watched, event);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		// The layers stay parented to the shared toolbar after a runtime
		// skin switch; they must never leak matrix chrome into another skin.
		if (SkinManager::instance()->currentSkinId() != QStringLiteral("matrix"))
			return;

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, false);
		if (layer == UnderCells)
			paintBoard(painter);
		else
			paintLamp(painter);
	}

private:
	// The badge is only board chrome while the skin owns its appearance.
	// MainWindow::updateDirtyStatus replaces it with an inline-styled pill
	// at runtime; painting a lamp under that pill would garble its text.
	QWidget* ownedBadge() const
	{
		QWidget* badge = parentWidget()->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge"));
		if (badge == nullptr || !badge->isVisible() || !badge->styleSheet().isEmpty())
			return nullptr;
		return badge;
	}

	void paintBoard(QPainter& painter)
	{
		// The faint 24px column grid: the graph paper the whole board sits
		// on, same pitch and ink as the card grid texture.
		QColor grid(ruleColor);
		grid.setAlpha(gridAlpha);
		painter.setPen(QPen(grid, 1));
		for (int x = MatrixMetrics::gridPitch; x < width(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, 0, x, height());

		// Doubled header rule above the QSS bottom border: the strip closes
		// like a board's title rule, not a plain window edge.
		painter.setPen(QPen(ruleColor, 1));
		painter.drawLine(0, height() - 4, width(), height() - 4);

		// Sunken fill behind the status readout cell (the badge's own QSS
		// background stays transparent so this fill and the lamp show).
		if (QWidget* badge = ownedBadge())
			painter.fillRect(badge->geometry().adjusted(1, 1, -1, -1), sunkenColor);
	}

	void paintLamp(QPainter& painter)
	{
		QWidget* badge = ownedBadge();
		if (badge == nullptr)
			return;
		// Solid square lamp in traffic-light semantics: green = saved,
		// amber = modified. This is exactly what colour rationing reserves
		// colour for - the one truthful lamp on the header strip.
		const QRect cell = badge->geometry();
		const QRect lampRect(cell.left() + 8, cell.center().y() - 4, 8, 8);
		painter.fillRect(lampRect, badge->property("dirty").toBool() ? modifiedColor : savedColor);
	}

	Layer layer;
	QColor ruleColor;
	QColor sunkenColor;
	QColor savedColor;
	QColor modifiedColor;
	int gridAlpha = 55;
};
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
		// The shared raw-preview strip is replaced by this skin's own caption
		// strip (MatrixRowCaption): same raw spec, but spoken in the board's
		// footer grammar and wired into the crosspoint hover echo (M2).
		t.showRawPreview = false;
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
		// Lit-segment luminance is calibrated per mode (M1): on the dark board
		// the LEDs gain headroom toward white so a lit cell clearly outshines
		// the ghost ring; the light tokens were derived for maximum contrast
		// on white, where lightening would only desaturate them.
		if (QColor(tokens.surface).lightness() < 128)
			litColor = litColor.lighter(112);
		if (state.dragging)
			litColor = litColor.lighter(125);
		else if (state.hovered)
			litColor = litColor.lighter(112);
		// The unlit ring stays visible at low alpha (M1): the range geometry -
		// and the bipolar centre gap - must read even with nothing lit, the
		// way an unlit LED is still a visible part on the board. Muted ink
		// instead of border ink, which vanished against the light card.
		QColor trackColor(mutedColor);
		trackColor.setAlpha(state.enabled ? 80 : 40);

		for (int i = 0; i < segmentCount; i++)
		{
			const double fraction = (i + 0.5) / segmentCount;
			const bool lit = state.enabled && i >= litFrom && i < litFrom + litCount;
			// A lit cell is wider than a ghost cell: LEDs bloom, rules do not.
			QPen segmentPen(lit ? litColor : trackColor, lit ? 3.5 : 2.5, Qt::SolidLine, Qt::FlatCap);
			painter.setPen(segmentPen);
			painter.drawLine(matrixRadialPoint(center, innerRadius, fraction),
				matrixRadialPoint(center, outerRadius, fraction));
		}

		// Centre detent tick: marks the 0-position gap of bipolar knobs so the
		// two knob kinds read differently even at rest. Full text ink (M1):
		// at 0 dB the gap plus this tick is the whole detent statement.
		if (state.bipolar)
		{
			painter.setPen(QPen(state.enabled ? QColor(tokens.text) : QColor(trackColor), 1.0, Qt::SolidLine, Qt::FlatCap));
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
		if (info.legacyRow)
			return;

		// The picker's coordinate language imported into the row (M2): the
		// plain line number becomes a board coordinate, the type's bus letter
		// ahead of the stable line position ("B3" = a BiQuad entry on line 3,
		// the picker's C1 cell grammar). Spacer rows are blank board lines
		// and carry no coordinate.
		QLabel* coordinateCell = nullptr;
		if (card != nullptr && header != nullptr && info.type != QStringLiteral("spacer"))
		{
			coordinateCell = header->findChild<QLabel*>(QStringLiteral("FilterCardNumber"));
			if (coordinateCell != nullptr)
			{
				bool plainNumber = false;
				const int line = coordinateCell->text().toInt(&plainNumber);
				if (plainNumber)
					coordinateCell->setText(matrixBusLetter(info.type) + QString::number(line));
			}

			// The caption strip docks under the card body and echoes the raw
			// spec next to that coordinate on hover (the picker footer's
			// grammar; see MatrixRowCaption).
			QVBoxLayout* cardLayout = qobject_cast<QVBoxLayout*>(card->layout());
			if (cardLayout != nullptr)
			{
				QLabel* rawSpec = card->findChild<QLabel*>(QStringLiteral("FilterCardRawPreview"));
				cardLayout->addWidget(new MatrixRowCaption(card, rawSpec, coordinateCell));
			}
		}

		if (body == nullptr)
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

		// Faint column grid: the graph paper the board sits on. Clipped to the
		// header band - maintainer review (issue #93) judged the texture a
		// distracting afterimage behind parameter widgets, so the row body
		// stays a calm opaque panel regardless of editor widget opacity.
		QColor gridColor(tokens.border);
		const int gridAlpha = QColor(tokens.surface).lightness() < 128 ? 80 : 90;
		gridColor.setAlpha(info.enabled ? gridAlpha : gridAlpha / 2);
		painter.setPen(QPen(gridColor, 1));
		for (int x = content.left() + MatrixMetrics::gridPitch; x < content.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, headerBand.top(), x, headerBand.bottom());

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

	// The board's masthead: the faint 24px column grid behind the title
	// readout and a doubled bottom rule (this inner line plus the QSS bottom
	// border), so the strip closes like a board's title rule, not a plain
	// window edge - the same construction MatrixToolbarBoard gives the
	// toolbar strip. The caption cells stay transparent in QSS so the grid
	// runs through them, exactly like the toolbar's function cells.
	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing, false);

		// The hook carries no mode flag; infer it from the surface lightness
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		QColor grid(tokens.border);
		grid.setAlpha(QColor(tokens.surface).lightness() < 128 ? 55 : 90);
		painter.setPen(QPen(grid, 1));
		for (int x = rect.left() + MatrixMetrics::gridPitch; x < rect.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, rect.top(), x, rect.bottom());

		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(rect.left(), rect.bottom() - 3, rect.right(), rect.bottom() - 3);
	}

	// The board's header strip. The neutral stroke icons stay, tinted in
	// plain ink (the catalog is monochrome - colour belongs to the status
	// lamp); the QSS dresses every toolbar item as a square 1px cell, and
	// two painted layers add what QSS cannot express: the 24px column grid
	// behind the cells and the solid square status lamp inside the
	// DirtyStatusBadge readout. Runs at startup and on every skin/dark
	// switch, so the layers are looked up again and re-tinted, never
	// created twice.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;

		// Shared modern stroke icons, tinted with the text token.
		ISkin::styleMainToolbar(toolBar, tokens);
		toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

		// Painted board layers: created once per toolbar, re-tinted on every
		// call (dark/light switches reuse the same instances).
		auto boardLayer = [toolBar](const QString& name, MatrixToolbarBoard::Layer layer)
		{
			QWidget* existing = toolBar->findChild<QWidget*>(name, Qt::FindDirectChildrenOnly);
			MatrixToolbarBoard* board = existing != nullptr
				? static_cast<MatrixToolbarBoard*>(existing)
				: new MatrixToolbarBoard(toolBar, layer);
			return board;
		};
		boardLayer(QStringLiteral("MatrixToolbarBoardUnder"), MatrixToolbarBoard::UnderCells)->setBoardTokens(tokens);
		boardLayer(QStringLiteral("MatrixToolbarBoardOver"), MatrixToolbarBoard::OverCells)->setBoardTokens(tokens);
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
