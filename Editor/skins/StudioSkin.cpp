/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Studio skin, split out of Skins.cpp (audit #109 F005). This is a verbatim
// move of the helpers and the class; behaviour is unchanged. The file-scope
// instance is exposed through studioSkin() so Skins::all() can assemble the
// roster without a central definition list.

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
#include "SkinSupport.h"

namespace
{
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
}

ISkin* studioSkin()
{
	static StudioSkin instance;
	return &instance;
}
