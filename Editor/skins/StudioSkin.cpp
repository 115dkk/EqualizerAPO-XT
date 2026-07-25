/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Constitution: docs/skins/studio.md
// The file-scope instance is exposed through studioSkin() so Skins::all()
// can assemble the roster without a central definition list.

#include "Skins.h"

#include <QAction>
#include <QComboBox>
#include <QDial>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolBar>
#include <QWidget>
#include <QtMath>

// Studio's S3 band-colour law maps BiQuad filter types onto hue families.
#include "filters/BiQuad.h"

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/cards/StudioReferenceCardView.h"
#include "Editor/skins/pickers/StudioFilterPicker.h"
#include "Editor/widgets/routing/LightTraceRoutingRenderer.h"
#include "SkinFileIcons.h"
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Studio (glass over the instrument, FabFilter-like) ──────────────────────
// Constitution: docs/skins/studio.md

// Band-colour law: the light a BiQuad row carries - knob arcs, type badge
// ink, signal lamp, hover/selected border glow - takes the row's band
// colour, one hue family per filter type. The glass itself stays neutral;
// rows that carry no "studioBand" tag keep the neutral accent.
const char* const studioBandFamilies[] = { "peak", "shelf", "pass", "notch" };

// File-dialog pictograms in Studio's language: thin rounded strokes in the
// toolbar's receded ink, no fills. The glass stays neutral; a pictogram is
// an engraving on it, not a sticker (the shell/default set read too toy-like
// next to the cards - user round-2 verdict).
class StudioFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			painter.setPen(QPen(ink, qMax(1.1, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);

			const auto docOutline = [&]() {
				QPainterPath path;
				path.moveTo(s * 0.24, s * 0.14);
				path.lineTo(s * 0.62, s * 0.14);
				path.lineTo(s * 0.76, s * 0.28);
				path.lineTo(s * 0.76, s * 0.86);
				path.lineTo(s * 0.24, s * 0.86);
				path.closeSubpath();
				painter.drawPath(path);
				painter.drawLine(QPointF(s * 0.62, s * 0.14), QPointF(s * 0.62, s * 0.28));
				painter.drawLine(QPointF(s * 0.62, s * 0.28), QPointF(s * 0.76, s * 0.28));
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				QPainterPath path;
				path.moveTo(s * 0.12, s * 0.78);
				path.lineTo(s * 0.12, s * 0.26);
				path.lineTo(s * 0.40, s * 0.26);
				path.lineTo(s * 0.48, s * 0.36);
				path.lineTo(s * 0.88, s * 0.36);
				path.lineTo(s * 0.88, s * 0.78);
				path.closeSubpath();
				painter.drawPath(path);
				break;
			}
			case Glyph::ConfigFile:
				docOutline();
				painter.drawLine(QPointF(s * 0.34, s * 0.50), QPointF(s * 0.66, s * 0.50));
				painter.drawLine(QPointF(s * 0.34, s * 0.66), QPointF(s * 0.58, s * 0.66));
				break;
			case Glyph::AudioFile:
			{
				docOutline();
				QPainterPath wave;
				wave.moveTo(s * 0.32, s * 0.60);
				wave.lineTo(s * 0.42, s * 0.46);
				wave.lineTo(s * 0.54, s * 0.70);
				wave.lineTo(s * 0.66, s * 0.54);
				painter.drawPath(wave);
				break;
			}
			case Glyph::PluginFile:
				docOutline();
				painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.22));
				painter.drawLine(QPointF(s * 0.44, s * 0.48), QPointF(s * 0.44, s * 0.40));
				painter.drawLine(QPointF(s * 0.56, s * 0.48), QPointF(s * 0.56, s * 0.40));
				break;
			case Glyph::GenericFile:
				docOutline();
				break;
			case Glyph::Drive:
				painter.drawRoundedRect(QRectF(s * 0.12, s * 0.32, s * 0.76, s * 0.38), s * 0.06, s * 0.06);
				painter.drawLine(QPointF(s * 0.20, s * 0.58), QPointF(s * 0.52, s * 0.58));
				painter.setBrush(ink);
				painter.drawEllipse(QPointF(s * 0.76, s * 0.58), s * 0.035, s * 0.035);
				break;
			case Glyph::Computer:
				painter.drawRoundedRect(QRectF(s * 0.14, s * 0.18, s * 0.72, s * 0.46), s * 0.05, s * 0.05);
				painter.drawLine(QPointF(s * 0.50, s * 0.64), QPointF(s * 0.50, s * 0.76));
				painter.drawLine(QPointF(s * 0.34, s * 0.80), QPointF(s * 0.66, s * 0.80));
				break;
			}
		});
	}
};

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

// The same band law keyed off the descriptor's type code (LS/LSC ride the
// shelf family, LP/HP/BP and their Q forms the pass family) - the badge
// pictogram's ink resolves from the config line before any type combo exists.
QString studioBandFamilyForBadgeToken(const QString& token)
{
	if (token.startsWith(QLatin1String("LS")) || token.startsWith(QLatin1String("HS")))
		return QStringLiteral("shelf");
	if (token.startsWith(QLatin1String("LP")) || token.startsWith(QLatin1String("HP")) || token.startsWith(QLatin1String("BP")))
		return QStringLiteral("pass");
	if (token.startsWith(QLatin1String("NO")) || token.startsWith(QLatin1String("AP")))
		return QStringLiteral("notch");
	return QStringLiteral("peak");
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
			hex = studioBandHex(family.toString(), skinIsDark(tokens));
	}
	return QColor(hex);
}

class StudioSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("studio"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static LightTraceRoutingRenderer renderer;
		return &renderer;
	}

	// The "add filter" picker as a floating frosted-glass panel
	// (StudioFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new StudioFilterPickerView(parent);
	}

	// Reference rows (Include / Convolution / MultiConvolution / VST)
	// (StudioReferenceCardView.cpp; the studio sheets carry the styling).
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new StudioReferenceCardView(kind, parent);
	}

	// The title bar: the QSS keeps the strip on the deep stage colour; this
	// hook lays a whisper of reflection along the top edge plus a faint
	// accent-to-violet arc caught on it. Both are plain strokes: bloom
	// first, then the core.
	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		painter.save();

		// The 1px lighter top edge, quieter than a panel's reflection.
		painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1.0),
			QColor(255, 255, 255, dark ? 30 : 235));

		painter.setRenderHint(QPainter::Antialiasing);
		const double span = rect.width() * 0.42;
		const double x0 = rect.left() + (rect.width() - span) / 2.0;
		const double y = rect.top() + 0.5;
		QLinearGradient bloom(x0, y, x0 + span, y);
		bloom.setColorAt(0.0, withAlpha(tokens.accent, 0));
		bloom.setColorAt(0.35, withAlpha(tokens.accent, dark ? 70 : 55));
		bloom.setColorAt(0.7, withAlpha(tokens.accent2, dark ? 52 : 42));
		bloom.setColorAt(1.0, withAlpha(tokens.accent2, 0));
		QPen bloomPen(QBrush(bloom), 4.0);
		bloomPen.setCapStyle(Qt::RoundCap);
		painter.setPen(bloomPen);
		painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));
		QLinearGradient core(x0, y, x0 + span, y);
		core.setColorAt(0.0, withAlpha(tokens.accent, 0));
		core.setColorAt(0.35, withAlpha(tokens.accent, dark ? 215 : 195));
		core.setColorAt(0.7, withAlpha(tokens.accent2, dark ? 175 : 155));
		core.setColorAt(1.0, withAlpha(tokens.accent2, 0));
		QPen corePen(QBrush(core), 1.5);
		corePen.setCapStyle(Qt::RoundCap);
		painter.setPen(corePen);
		painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));

		painter.restore();
	}

	// The QSS sheets own the toolbar strip itself; code only re-inks the
	// file actions as quiet ink - the muted colour lifted halfway toward
	// the text ink - so the icons stay legible at menu size. Idempotent:
	// re-tinting and re-sizing converge on every skin/dark switch.
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
			else if (action->objectName() == QStringLiteral("actionUndo"))
				action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/undo.svg"), ink, 18));
			else if (action->objectName() == QStringLiteral("actionRedo"))
				action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/redo.svg"), ink, 18));
		}
	}

	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override
	{
		if (dialog == nullptr)
			return;
		// The shared stroke set in the toolbar's half-muted ink, so the
		// dialog chrome recedes behind the file data the same way the main
		// toolbar recedes behind the cards.
		const QColor text(tokens.text);
		const QColor muted(tokens.mutedText);
		const QColor ink((muted.red() + text.red()) / 2,
			(muted.green() + text.green()) / 2,
			(muted.blue() + text.blue()) / 2);
		SkinTokens recededTokens = tokens;
		recededTokens.text = ink.name(QColor::HexRgb);
		ISkin::styleFileDialog(dialog, recededTokens);
		// The folder/file pictograms are engravings in the same receded ink.
		static StudioFileIconProvider iconProvider;
		iconProvider.updateTokens(recededTokens);
		dialog->setIconProvider(&iconProvider);
	}

	// "The arc IS the value": no knob body, only a thin track circle, a
	// glowing arc in the row's band colour and a small indicator dot. The
	// numeric readout fades in while hovering or dragging; disabled knobs
	// drop to reduced opacity.
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

		// Luminance ladder: rest keeps a faint outer stroke so the arc glows
		// even untouched, hover blooms one full step and a drag turns the
		// light all the way up.
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

		// 0 dB anchor: a luminous tick crossing the track at 12 o'clock,
		// drawn over the arc so the centre detent stays readable even at
		// small gains - bloom first, bright core on top. At 0 dB the
		// indicator dot sits right under it.
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

	// Glass card: alpha fill + 1px lighter top edge; paintCardChrome layers
	// the caught light on top. Command types announce themselves through
	// the border treatment (DSP solid, Include dashed, VST a vertical
	// accent gradient), and tagged BiQuad rows hang their hover/selection
	// glow on their band colour.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);

		if (!info.enabled)
		{
			return QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 8px; }")
				.arg(cssRgba(tokens.card, 0.45), cssRgba(tokens.border, 0.55));
		}

		const QString background = cssRgba(info.selected ? tokens.cardSelected : tokens.card, 0.88);
		const QString hoverBackground = cssRgba(info.selected ? tokens.cardSelected : tokens.cardHover, 0.94);
		const QString topEdge = dark ? QStringLiteral("rgba(255, 255, 255, 0.10)") : QStringLiteral("rgba(255, 255, 255, 0.95)");
		const QString topEdgeHover = dark ? QStringLiteral("rgba(255, 255, 255, 0.17)") : QStringLiteral("#FFFFFF");

		if (info.type == QStringLiteral("vst"))
		{
			// The gradient border is the glow; no separate top edge needed.
			const QString gradient = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
				.arg(cssRgba(tokens.accent, info.focused || info.selected ? 0.95 : 0.70),
					cssRgba(tokens.accent, info.focused || info.selected ? 0.45 : 0.22));
			const QString hoverGradient = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
				.arg(cssRgba(tokens.accent, 0.95), cssRgba(tokens.accent, 0.40));
			return QStringLiteral(
				"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 8px; }"
				" QFrame#FilterCardRow:hover { background: %3; border-color: %4; }")
				.arg(background, gradient, hoverBackground, hoverGradient);
		}

		const QString borderStyle = info.type == QStringLiteral("include")
			? QStringLiteral("dashed") : QStringLiteral("solid");
		const QString borderBrush = info.focused
			? tokens.focusRing
			: (info.selected ? cssRgba(tokens.accent, 0.65) : cssRgba(tokens.border, 0.90));
		const QString hoverBorderBrush = info.focused ? tokens.focusRing : cssRgba(tokens.accent, 0.45);

		QString style = QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-top-color: %4; border-radius: 8px; }"
			" QFrame#FilterCardRow:hover { background: %5; border-color: %6; border-top-color: %7; }")
			.arg(background, borderStyle, borderBrush, topEdge, hoverBackground, hoverBorderBrush, topEdgeHover);

		// A tagged BiQuad row's border light follows its band colour.
		// Attribute selectors outrank the base rules, and untagged rows can
		// never match them; keyboard focus keeps the neutral focus ring.
		if (info.type == QStringLiteral("biquad") && !info.focused)
		{
			for (const char* family : studioBandFamilies)
			{
				const QString band = studioBandHex(QLatin1String(family), dark);
				if (info.selected)
					style += QStringLiteral(" QFrame#FilterCardRow[studioBand=\"%1\"] { border-color: %2; border-top-color: %3; }")
						.arg(QLatin1String(family), cssRgba(band, 0.65), topEdge);
				style += QStringLiteral(" QFrame#FilterCardRow[studioBand=\"%1\"]:hover { border-color: %2; border-top-color: %3; }")
					.arg(QLatin1String(family), cssRgba(band, 0.45), topEdgeHover);
			}
		}
		return style;
	}

	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
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

	// Painted decoration on top of the QSS chrome: the pane treatment
	// (frost sheen, centre-bright reflection, bottom shade), the signal
	// lamp on DSP rows and the VST halo. Include/comment/raw and If/Eval
	// rows stay unlit panes; disabled rows paint nothing.
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		if (!info.enabled || info.type == QStringLiteral("spacer"))
			return;

		const bool dark = skinIsDark(tokens);
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing);

		// The pane: the glass surface treatment stays inside the border.
		const QRectF pane = QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0);
		QPainterPath panePath;
		panePath.addRoundedRect(pane, 7.0, 7.0);
		painter.setClipPath(panePath);

		if (dark)
		{
			// Frost sheen in the upper glass.
			QLinearGradient sheen(pane.topLeft(), QPointF(pane.left(), pane.top() + pane.height() * 0.45));
			sheen.setColorAt(0.0, QColor(255, 255, 255, info.hovered ? 24 : 15));
			sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.fillPath(panePath, sheen);
		}

		// Shade pooling at the bottom edge; in light mode this shade carries
		// the whole glass impression.
		QLinearGradient depthShade(QPointF(pane.left(), pane.bottom() - pane.height() * 0.38), pane.bottomLeft());
		depthShade.setColorAt(0.0, QColor(0, 0, 0, 0));
		depthShade.setColorAt(1.0, dark ? QColor(0, 0, 0, 52) : QColor(24, 32, 51, 26));
		painter.fillPath(panePath, depthShade);

		if (dark)
		{
			// Centre-bright reflection just under the border's top edge.
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
			|| info.type == QStringLiteral("include")
			|| info.type == QStringLiteral("if") || info.type == QStringLiteral("eval"))
		{
			// Unlit panes: the glass surface only, no lamp. The If/Eval state
			// lives in the gutter's gate beam (paintScopeGutter), never as a
			// tint on the glass or the ink.
			//
			// An Eval row the last analysis run resolved shows "= value" at
			// the header's right. It is painted here rather than tagged in
			// prepareCommandRow because the fact refreshes with every
			// analysis run and only paint time sees fresh values. The summary
			// label bounds the readout, so a long expression keeps the right
			// of way and the value yields.
			if (info.type == QStringLiteral("eval") && !info.evalText.isEmpty() && !info.valueError
				&& painter.device() != nullptr && painter.device()->devType() == QInternal::Widget)
			{
				const QWidget* frame = static_cast<const QWidget*>(painter.device());
				const QLabel* summary = frame->findChild<QLabel*>(QStringLiteral("FilterCardSummary"));
				if (summary != nullptr && summary->isVisible())
				{
					const QRectF span(summary->mapTo(frame, QPoint(0, 0)), QSizeF(summary->size()));
					QFont readoutFont(tokens.monoFontFamily);
					readoutFont.setPointSizeF(8.0);
					const QFontMetricsF readoutMetrics(readoutFont);
					const QString readout = QStringLiteral("= ") + info.evalText;
					const QFontMetricsF summaryMetrics(summary->font());
					const double summaryWidth = summary->text().isEmpty()
						? 0.0 : summaryMetrics.horizontalAdvance(summary->text()) + 16.0;
					if (summaryWidth + readoutMetrics.horizontalAdvance(readout) <= span.width())
					{
						painter.setFont(readoutFont);
						// The header sheen washes this ink toward white in
						// light mode (S1), so the light ink starts at full
						// text strength and lands muted; dark stays muted.
						painter.setPen(dark ? withAlpha(tokens.mutedText, 225) : QColor(tokens.text));
						painter.drawText(span, Qt::AlignRight | Qt::AlignVCenter, readout);
					}
				}
			}
			painter.restore();
			return;
		}

		if (info.type == QStringLiteral("vst"))
		{
			// Two strokes hugging the border fake an outer glow; hover turns
			// the light up a full step.
			const QRectF edge = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(tokens.accent, info.hovered ? 150 : 80), 1.0));
			painter.drawRoundedRect(edge, 8.0, 8.0);
			painter.setPen(QPen(withAlpha(tokens.accent, info.hovered ? 80 : 36), 3.0));
			painter.drawRoundedRect(edge.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);
		}
		else
		{
			// Signal lamp in the row's band colour, blooming on hover.
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

	// The If-block scope is a gate beam: the knob arc's stroke ladder turned
	// vertical, flowing down the gutter in the base accent only. State is
	// light intensity; the If/ElseIf/Else stations answer with the
	// indicator-dot grammar. Level math: for members the if-lanes are the
	// innermost logicDepth bands after the depth - logicDepth outer channel
	// bands, and branch/tail rows mount at member depth
	// (logicSiblingsIndentAsMembers) so the beam passes their faces instead
	// of dying behind them.
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool ifFamily = info.type == QStringLiteral("if");
		const bool headRow = ifFamily && info.command == QStringLiteral("if");
		const bool branchRow = ifFamily && (info.command == QStringLiteral("elseif") || info.command == QStringLiteral("else"));
		const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
		const int logic = info.logicDepth;
		if (!headRow && logic <= 0)
			return false;

		const int unit = tokens.channelGroupIndent;
		const double h = size.height();
		const double junctionY = 4.0 + tokens.rowHeight / 2.0;
		const int indentUnits = (ifFamily && !headRow) ? info.depth + 1 : info.depth;

		const QColor beam(tokens.accent);
		const bool live = info.enabled && !info.lineSkipped;

		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);

		const auto bandCenter = [unit](int level) { return 8.0 + level * unit + unit / 2.0; };

		// One run of the beam: a lit run carries the full ladder, a
		// de-energized run only a faint residual core. A fading run (the
		// EndIf termination) holds its light for half the drop and dies
		// before the end.
		const auto beamRun = [&](int level, double y0, double y1, bool runLive, bool fadeOut) {
			struct Stroke { double width; int alpha; };
			static const Stroke litLadder[] = { { 7.0, 24 }, { 3.4, 64 }, { 1.4, 210 } };
			static const Stroke deadLadder[] = { { 3.4, 14 }, { 1.2, 64 } };
			const Stroke* ladder = runLive ? litLadder : deadLadder;
			const int count = runLive ? 3 : 2;
			const double x = bandCenter(level);
			for (int i = 0; i < count; i++)
			{
				QPen pen;
				if (fadeOut)
				{
					QLinearGradient dying(QPointF(x, y0), QPointF(x, y1));
					dying.setColorAt(0.0, withAlpha(beam, ladder[i].alpha));
					dying.setColorAt(0.55, withAlpha(beam, ladder[i].alpha));
					dying.setColorAt(1.0, withAlpha(beam, 0));
					pen = QPen(QBrush(dying), ladder[i].width);
				}
				else
				{
					pen = QPen(withAlpha(beam, ladder[i].alpha), ladder[i].width);
				}
				pen.setCapStyle(Qt::FlatCap);
				painter.setPen(pen);
				painter.drawLine(QPointF(x, y0), QPointF(x, y1));
			}
			painter.setPen(Qt::NoPen);
		};

		// The station's answer: the indicator-dot grammar (halo + core); the
		// unlit dot is a glass core under a neutral hairline.
		const auto anchor = [&](double cx, double cy, bool litDot, double coreRadius) {
			if (litDot)
			{
				painter.setBrush(withAlpha(beam, 110));
				painter.drawEllipse(QPointF(cx, cy), coreRadius * 2.0, coreRadius * 2.0);
				painter.setBrush(beam);
				painter.drawEllipse(QPointF(cx, cy), coreRadius, coreRadius);
			}
			else
			{
				painter.setPen(QPen(withAlpha(tokens.border, 220), 1.0));
				painter.setBrush(QColor(tokens.card));
				painter.drawEllipse(QPointF(cx, cy), coreRadius, coreRadius);
				painter.setPen(Qt::NoPen);
				painter.setBrush(Qt::NoBrush);
			}
		};

		// Outer channel-group levels keep the quiet gradient bar (the shared
		// default's geometry) in neutral ink.
		const auto channelRail = [&](int level) {
			QLinearGradient bar(0, 0, 0, h);
			bar.setColorAt(0.0, withAlpha(tokens.border, 70));
			bar.setColorAt(1.0, withAlpha(tokens.border, 16));
			painter.fillRect(QRectF(8.0 + level * unit + 7.0, 0.0, 3.0, h), bar);
		};

		const int ifLevels = headRow ? logic : (branchRow || tailRow) ? logic - 1 : logic;
		const int channelLevels = qMax(0, indentUnits - ifLevels - ((branchRow || tailRow) ? 1 : 0));
		for (int level = 0; level < channelLevels; level++)
			channelRail(level);

		if (headRow)
		{
			// The head sits at its semantic depth, so its block's beam only
			// peeks out of the bottom margin, under the station's anchor dot.
			for (int level = channelLevels; level < channelLevels + logic; level++)
				beamRun(level, 0.0, h, true, false);
			const int own = channelLevels + logic;
			beamRun(own, h - 4.0, h, live && info.branchState != 0, false);
			anchor(bandCenter(own), h - 2.0, info.branchState == 1, 2.2);
		}
		else if (branchRow)
		{
			// The chain's beam passes the ElseIf/Else face; the anchor on it
			// reports this branch. The runs between stations belong to the
			// member rows, which dim their own swallowed segments.
			for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
				beamRun(level, 0.0, h, true, false);
			const int own = channelLevels + logic - 1;
			beamRun(own, 0.0, h, live, false);
			anchor(bandCenter(own), junctionY, info.branchState == 1, 3.0);
		}
		else if (tailRow)
		{
			// The beam terminates on the EndIf row: it fades out above the
			// header line - no cap, no anchor.
			for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
				beamRun(level, 0.0, h, true, false);
			beamRun(channelLevels + logic - 1, 0.0, junctionY + 4.0, live, true);
		}
		else
		{
			// A member card: the innermost lane is its block's beam and takes
			// the row's own fate - a swallowed line de-energizes only its own
			// run, the outer lanes stay lit for the rows below.
			for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
				beamRun(level, 0.0, h, true, false);
			beamRun(channelLevels + logic - 1, 0.0, h, live, false);
		}

		painter.restore();
		return true;
	}

	// Branch/tail rows mount one indent unit past their semantic level so
	// the gate beam passes them instead of dying behind their full-width
	// faces.
	bool logicSiblingsIndentAsMembers() const override
	{
		return true;
	}

	// The trailing add row: a glass slot not yet fitted with a pane. Rest is
	// switched-off glass under a faint SOLID hairline (dashed is Include's
	// material; this slot is a place, not a reference); hover fits the
	// pane, pressing turns the light one step further up.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		const bool lit = state.hovered || state.pressed;
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF frame = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

		// Fill ladder: the disabled cards' dead-pane alpha at rest, rising
		// toward (but never reaching) the live cards' 0.88 when lit.
		const double fillAlpha = state.pressed ? 0.80 : (state.hovered ? 0.66 : 0.42);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(tokens.card, qRound(fillAlpha * 255)));
		painter.drawRoundedRect(frame, 8.0, 8.0);

		if (lit && !dark)
		{
			// The lit white slot cannot brighten, so a shade pooling at the
			// bottom edge carries the glass impression.
			QPainterPath panePath;
			panePath.addRoundedRect(frame.adjusted(1.0, 1.0, -1.0, -1.0), 7.0, 7.0);
			QLinearGradient depthShade(QPointF(frame.left(), frame.bottom() - frame.height() * 0.5), frame.bottomLeft());
			depthShade.setColorAt(0.0, QColor(24, 32, 51, 0));
			depthShade.setColorAt(1.0, QColor(24, 32, 51, state.pressed ? 34 : 26));
			painter.fillPath(panePath, depthShade);
		}

		// Base outline: a faint neutral hairline; keyboard focus wears the
		// neutral focus ring (the accent halo below stays a pointer answer).
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : withAlpha(tokens.border, lit ? 230 : 140), 1.0));
		painter.drawRoundedRect(frame, 8.0, 8.0);

		if (lit)
		{
			// Two border-hugging strokes fake the halo; press is one ladder
			// step up.
			painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 170 : 120), 1.0));
			painter.drawRoundedRect(frame, 8.0, 8.0);
			painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 80 : 48), 3.0));
			painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);

			if (dark)
			{
				// The centre-bright reflection lights on the fitted pane
				// (the card chrome's line, one step calmer).
				const double y = frame.top() + 1.5;
				QLinearGradient reflection(frame.left(), y, frame.right(), y);
				reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
				reflection.setColorAt(0.5, QColor(255, 255, 255, state.pressed ? 96 : 72));
				reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
				painter.setPen(QPen(QBrush(reflection), 1.0));
				painter.drawLine(QPointF(frame.left() + 7.0, y), QPointF(frame.right() - 7.0, y));
			}
		}

		// Caption: drawn plus + translated label, centred as one unit.
		QFont captionFont(tokens.fontFamily);
		captionFont.setPointSizeF(9.5);
		captionFont.setWeight(QFont::DemiBold);
		const QFontMetricsF metrics(captionFont);
		const double plusRadius = 4.0;
		const double gap = 8.0;
		const double textWidth = metrics.horizontalAdvance(state.label);
		const double totalWidth = plusRadius * 2.0 + gap + textWidth;
		const double left = frame.center().x() - totalWidth / 2.0;
		const QPointF plusCenter(left + plusRadius, frame.center().y());

		if (lit)
		{
			// The plus lights in the accent: bloom stroke first, core on top.
			painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 96 : 70), 4.5, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(plusCenter.x() - plusRadius, plusCenter.y()), QPointF(plusCenter.x() + plusRadius, plusCenter.y()));
			painter.drawLine(QPointF(plusCenter.x(), plusCenter.y() - plusRadius), QPointF(plusCenter.x(), plusCenter.y() + plusRadius));
			painter.setPen(QPen(QColor(tokens.accent), 1.6, Qt::SolidLine, Qt::RoundCap));
		}
		else
		{
			painter.setPen(QPen(withAlpha(tokens.mutedText, 200), 1.6, Qt::SolidLine, Qt::RoundCap));
		}
		painter.drawLine(QPointF(plusCenter.x() - plusRadius, plusCenter.y()), QPointF(plusCenter.x() + plusRadius, plusCenter.y()));
		painter.drawLine(QPointF(plusCenter.x(), plusCenter.y() - plusRadius), QPointF(plusCenter.x(), plusCenter.y() + plusRadius));

		painter.setFont(captionFont);
		painter.setPen(lit ? QColor(tokens.text) : QColor(tokens.mutedText));
		painter.drawText(QRectF(left + plusRadius * 2.0 + gap, frame.top(), textWidth + 4.0, frame.height()),
			Qt::AlignLeft | Qt::AlignVCenter, state.label);
	}

	// The first-boundary seam: light seeping between panes. The hosting
	// widget paints nothing at rest (shared contract); under the cursor a
	// horizontal ray crosses the boundary, with accent2 reserved for the
	// far end and the indicator dot marking the insertion point.
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		if (!state.hovered && !state.pressed)
			return;

		painter.setRenderHint(QPainter::Antialiasing);
		const double y = rect.center().y() + 0.5;
		const double x0 = rect.left() + 2.0;
		const double x1 = rect.right() - 2.0;
		const bool pressed = state.pressed;

		const auto ray = [&](int accentAlpha, int violetAlpha) {
			QLinearGradient gradient(x0, y, x1, y);
			gradient.setColorAt(0.0, withAlpha(tokens.accent, 0));
			gradient.setColorAt(0.28, withAlpha(tokens.accent, accentAlpha));
			gradient.setColorAt(0.74, withAlpha(tokens.accent2, violetAlpha));
			gradient.setColorAt(1.0, withAlpha(tokens.accent2, 0));
			return gradient;
		};

		// Bloom, mid, core: the knob arc's stroke ladder laid flat.
		QPen bloomPen(QBrush(ray(pressed ? 96 : 64, pressed ? 74 : 48)), 5.0);
		bloomPen.setCapStyle(Qt::RoundCap);
		painter.setPen(bloomPen);
		painter.drawLine(QPointF(x0, y), QPointF(x1, y));
		QPen midPen(QBrush(ray(pressed ? 205 : 150, pressed ? 165 : 118)), 2.4);
		midPen.setCapStyle(Qt::RoundCap);
		painter.setPen(midPen);
		painter.drawLine(QPointF(x0, y), QPointF(x1, y));
		QPen corePen(QBrush(ray(255, 235)), 1.0);
		corePen.setCapStyle(Qt::RoundCap);
		painter.setPen(corePen);
		painter.drawLine(QPointF(x0, y), QPointF(x1, y));

		// Insertion point: the indicator dot (halo + core) on the ray's
		// accent stretch.
		const QPointF dot(rect.center().x(), y);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(tokens.accent, pressed ? 130 : 100));
		painter.drawEllipse(dot, 4.4, 4.4);
		painter.setBrush(QColor(tokens.accent));
		painter.drawEllipse(dot, 2.2, 2.2);
	}

	// The GraphicEQ card's response plot: the widget owns the model and
	// every gesture; this hook owns every pixel. AA off on straight lines;
	// the curve is data and keeps AA.
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		const bool lit = state.enabled;
		const QRectF plot = state.plotRect;

		painter.save();
		if (!lit)
			painter.setOpacity(0.45);

		// Sunken pane: deep ground behind the one 8px round.
		const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
		QPainterPath pane;
		pane.addRoundedRect(frame, 8.0, 8.0);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.fillPath(pane, QColor(tokens.graph));
		painter.setClipPath(pane);

		// Grid: crisp 1px lines held far behind the data.
		painter.setRenderHint(QPainter::Antialiasing, false);
		const QColor gridMinor = withAlpha(tokens.graphGridMinor, dark ? 84 : 150);
		const QColor gridMajor = withAlpha(tokens.graphGridMajor, dark ? 118 : 165);
		for (const GraphicEQPlotState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(x, int(plot.top()), x, int(plot.bottom()));
		}
		for (const GraphicEQPlotState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(int(plot.left()), y, int(plot.right()), y);
		}

		// Margin labels: the in-between ticks' labels recede one step
		// further than the majors.
		QFont labelFont(tokens.monoFontFamily);
		labelFont.setPointSizeF(7.5);
		painter.setFont(labelFont);
		for (const GraphicEQPlotState::GridLine& line : state.vertical)
		{
			if (line.label.isEmpty())
				continue;
			painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
			painter.drawText(QRect(int(line.pos) - 24, int(plot.bottom()) + 2, 48,
				state.rect.bottom() - int(plot.bottom()) - 2),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
		for (const GraphicEQPlotState::GridLine& line : state.horizontal)
		{
			if (line.label.isEmpty())
				continue;
			painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
			painter.drawText(QRect(state.rect.left(), int(line.pos) - 8,
				int(plot.left()) - state.rect.left() - 5, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}

		// 0 dB: the knob's luminous anchor laid flat - accent bloom first,
		// text-ink core on top. When the light is off the anchor drops to a
		// quiet muted line.
		if (state.zeroY >= plot.top() && state.zeroY <= plot.bottom())
		{
			const int y = int(state.zeroY);
			if (lit)
			{
				painter.setPen(QPen(withAlpha(tokens.accent, 52), 3));
				painter.drawLine(int(plot.left()), y, int(plot.right()), y);
				painter.setPen(QPen(withAlpha(tokens.text, 200), 1));
			}
			else
			{
				painter.setPen(QPen(withAlpha(tokens.mutedText, 170), 1));
			}
			painter.drawLine(int(plot.left()), y, int(plot.right()), y);
		}

		painter.setRenderHint(QPainter::Antialiasing, true);
		const double base = qBound(plot.top(), state.zeroY, plot.bottom());

		if (state.curve.size() >= 2)
		{
			if (lit)
			{
				// The fill sinks from the curve toward the 0 dB line and
				// dies as it lands: a vertical gradient whose alpha peaks
				// away from the baseline on both sides.
				QPolygonF fill = state.curve;
				fill.append(QPointF(state.curve.last().x(), base));
				fill.prepend(QPointF(state.curve.first().x(), base));
				const double zeroRatio = qBound(0.02, (base - plot.top()) / qMax(1.0, plot.height()), 0.98);
				QLinearGradient sink(0, plot.top(), 0, plot.bottom());
				sink.setColorAt(0.0, withAlpha(tokens.accent, 52));
				sink.setColorAt(zeroRatio, withAlpha(tokens.accent, 7));
				sink.setColorAt(1.0, withAlpha(tokens.accent, 44));
				painter.setPen(Qt::NoPen);
				painter.setBrush(sink);
				painter.drawPolygon(fill);
			}

			// The curve: four layered strokes, wide and faint to narrow and
			// full. Lights-out keeps one thin stroke - the data survives,
			// the glow does not.
			painter.setBrush(Qt::NoBrush);
			const struct { double width; int alpha; } layers[] = {
				{ 9.0, 22 },
				{ 5.5, 48 },
				{ 3.0, 110 },
				{ 1.6, 255 }
			};
			for (const auto& layer : layers)
			{
				if (!lit && layer.width > 1.6)
					continue;
				QPen glow(withAlpha(tokens.accent, lit ? layer.alpha : 150), layer.width);
				glow.setCapStyle(Qt::RoundCap);
				glow.setJoinStyle(Qt::RoundJoin);
				painter.setPen(glow);
				painter.drawPolyline(state.curve);
			}
		}

		// Band-locked layouts: light stems rising from the baseline to each
		// band level - bloom under core.
		if (state.bandLocked)
		{
			for (const QPointF& node : state.nodePositions)
			{
				if (lit)
				{
					painter.setPen(QPen(withAlpha(tokens.accent, 36), 4.0));
					painter.drawLine(QPointF(node.x(), base), node);
					painter.setPen(QPen(withAlpha(tokens.accent, 150), 1.6));
				}
				else
				{
					painter.setPen(QPen(withAlpha(tokens.accent, 90), 1.2));
				}
				painter.drawLine(QPointF(node.x(), base), node);
			}
		}

		// Nodes: indicator dots on the luminance ladder; selection adds an
		// outer bloom, keyboard focus a thin ring.
		for (int i = 0; i < state.nodePositions.size(); i++)
		{
			const QPointF& center = state.nodePositions.at(i);
			const bool selected = state.selectedNodes.contains(i);
			const bool hovered = state.hoveredNode == i;
			if (lit)
			{
				painter.setPen(Qt::NoPen);
				if (selected)
				{
					painter.setBrush(withAlpha(tokens.accent, 40));
					painter.drawEllipse(center, 9.0, 9.0);
				}
				painter.setBrush(withAlpha(tokens.accent, selected ? 120 : (hovered ? 88 : 36)));
				painter.drawEllipse(center, 6.0, 6.0);
				painter.setBrush(QColor(tokens.accent));
				painter.drawEllipse(center, 3.0, 3.0);
			}
			else
			{
				painter.setPen(QPen(withAlpha(tokens.border, 220), 1.0));
				painter.setBrush(QColor(tokens.card));
				painter.drawEllipse(center, 2.8, 2.8);
			}
			if (lit && state.focused && state.focusedNode == i)
			{
				painter.setPen(QPen(withAlpha(tokens.accent, 110), 1.0));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(center, 8.5, 8.5);
			}
		}

		// Cursor readout: dim mono on the glass, alive only under the
		// pointer.
		if (lit && state.cursorValid && !state.cursorText.isEmpty())
		{
			painter.setFont(labelFont);
			painter.setPen(withAlpha(tokens.mutedText, 220));
			painter.drawText(QRectF(plot.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
		}

		// The pane's edge: a hairline border (the focus ring when the
		// keyboard holds the plot) over a darker inner top edge.
		painter.setClipping(false);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(state.focused && lit ? QColor(tokens.focusRing) : withAlpha(tokens.border, lit ? 255 : 150), 1.0));
		painter.drawRoundedRect(frame, 8.0, 8.0);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.fillRect(QRectF(frame.left() + 7.0, frame.top() + 1.0, frame.width() - 14.0, 1.0),
			dark ? QColor(0, 0, 0, lit ? 140 : 80) : QColor(0, 0, 0, lit ? 30 : 16));

		painter.restore();
	}

	// The analysis dock's response graph: the GraphicEQ gauge widened into
	// an always-on monitor (sunken pane, crisp grid, 0 dB anchor,
	// four-stroke trace over a split under-fill, danger clip treatment,
	// light-seam cursor). The cursor group fades in on state.hover.
	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		const QRectF plot = state.plotRect;
		const double hover = qBound(0.0, state.hover, 1.0);

		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		// Sunken pane: the deep graph ground behind the one 8px round.
		const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
		QPainterPath pane;
		pane.addRoundedRect(frame, 8.0, 8.0);
		painter.fillPath(pane, QColor(tokens.graph));
		painter.setClipPath(pane);

		// The pane's inner light answers hover. Dark: a frost sheen settling
		// from the top. Light: the thickness shade pooling at the bottom
		// deepens instead (white glass cannot brighten).
		if (dark)
		{
			QLinearGradient sheen(frame.topLeft(), QPointF(frame.left(), frame.top() + frame.height() * 0.45));
			sheen.setColorAt(0.0, QColor(255, 255, 255, qRound(6.0 + 12.0 * hover)));
			sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.fillPath(pane, sheen);
		}
		else
		{
			QLinearGradient depthShade(QPointF(frame.left(), frame.bottom() - frame.height() * 0.38), frame.bottomLeft());
			depthShade.setColorAt(0.0, QColor(24, 32, 51, 0));
			depthShade.setColorAt(1.0, QColor(24, 32, 51, qRound(18.0 + 8.0 * hover)));
			painter.fillPath(pane, depthShade);
		}

		// Grid: crisp 1px lines held far behind the data.
		painter.setRenderHint(QPainter::Antialiasing, false);
		const QColor gridMinor = withAlpha(tokens.graphGridMinor, dark ? 84 : 150);
		const QColor gridMajor = withAlpha(tokens.graphGridMajor, dark ? 118 : 165);
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(int(line.pos), int(plot.top()), int(line.pos), int(plot.bottom()));
		}
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
		{
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(int(plot.left()), int(line.pos), int(plot.right()), int(line.pos));
		}

		// Axis figures: minors one step dimmer; frequency figures under
		// their ticks, dB figures inside the pane's left edge. Tight fits
		// shed minor labels first.
		QFont labelFont(tokens.monoFontFamily);
		labelFont.setPointSizeF(7.5);
		painter.setFont(labelFont);
		const int vCount = state.vertical.size();
		const double vSpacing = vCount > 1 ? plot.width() / (vCount - 1) : plot.width();
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			if (line.label.isEmpty() || (!line.major && vSpacing < 30.0))
				continue;
			painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
			painter.drawText(QRect(int(line.pos) - 24, int(plot.bottom()) + 2, 48, 11),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
		const int hCount = state.horizontal.size();
		const double hSpacing = hCount > 1 ? plot.height() / (hCount - 1) : plot.height();
		const int hLabelStep = hSpacing >= 13.0 ? 1 : (hSpacing >= 6.5 ? 2 : 4);
		for (int i = 0; i < hCount; i++)
		{
			const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
			if (line.label.isEmpty() || (!line.major && (i % hLabelStep) != 0))
				continue;
			painter.setPen(withAlpha(tokens.mutedText, line.major ? 200 : 130));
			const double labelY = qBound(plot.top() + 2.0, line.pos - 11.0, plot.bottom() - 12.0);
			painter.drawText(QRectF(plot.left() + 5.0, labelY, 44.0, 10.0),
				Qt::AlignLeft | Qt::AlignVCenter, line.label);
		}

		// Footer caption: the channel/sample-rate readout (localized data,
		// drawn as-is) centred under the tick figures.
		if (!state.channelText.isEmpty())
		{
			const QFontMetricsF captionMetrics(labelFont);
			painter.setPen(withAlpha(tokens.mutedText, 190));
			painter.drawText(QRectF(plot.left(), plot.bottom() + 14.0, plot.width(),
				qMax(0.0, frame.bottom() - plot.bottom() - 14.0)),
				Qt::AlignHCenter | Qt::AlignTop,
				captionMetrics.elidedText(state.channelText, Qt::ElideRight, plot.width()));
		}

		// Zero: the knob's luminous anchor laid flat - accent bloom under a
		// text-ink core. Drawn only when the metric's zero is inside the fitted
		// range; a group delay that never goes negative has no anchor to lay.
		if (state.zeroVisible)
		{
			const int zy = int(state.zeroY);
			painter.setPen(QPen(withAlpha(tokens.accent, 52), 3));
			painter.drawLine(int(plot.left()), zy, int(plot.right()), zy);
			painter.setPen(QPen(withAlpha(tokens.text, 200), 1));
			painter.drawLine(int(plot.left()), zy, int(plot.right()), zy);
		}

		painter.setRenderHint(QPainter::Antialiasing, true);
		const double zeroClamped = qBound(plot.top(), state.zeroY, plot.bottom());

		// Clipping warms the glass above 0 dB: a danger wash dying as it
		// lands on the anchor.
		if (state.clipping && zeroClamped > plot.top() + 1.0)
		{
			QLinearGradient warmth(0, plot.top(), 0, zeroClamped);
			warmth.setColorAt(0.0, withAlpha(tokens.danger, dark ? 34 : 26));
			warmth.setColorAt(1.0, withAlpha(tokens.danger, 0));
			painter.fillRect(QRectF(plot.left(), plot.top(), plot.width(), zeroClamped - plot.top()), warmth);
		}

		// One pass per segment. The response breaks where the metric has no
		// value, and the glass must break with it - a bridging stroke would
		// glow across a reading the config never produced.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;

			// The under-fill splits at zero: boost glows a step warmer than
			// cut, both dying as they land on the anchor.
			QPolygonF fill = segment;
			fill.append(QPointF(segment.last().x(), zeroClamped));
			fill.prepend(QPointF(segment.first().x(), zeroClamped));
			const double zeroRatio = qBound(0.02, (zeroClamped - plot.top()) / qMax(1.0, plot.height()), 0.98);
			QLinearGradient split(0, plot.top(), 0, plot.bottom());
			split.setColorAt(0.0, withAlpha(tokens.accent, 62));
			split.setColorAt(zeroRatio, withAlpha(tokens.accent, 8));
			split.setColorAt(1.0, withAlpha(tokens.accent, 34));
			painter.setPen(Qt::NoPen);
			painter.setBrush(split);
			painter.drawPolygon(fill);

			// The trace: four layered strokes, the glow lifted a breath
			// while the pointer holds the pane.
			painter.setBrush(Qt::NoBrush);
			const struct { double width; int alpha; int lift; } layers[] = {
				{ 9.0, 22, 10 },
				{ 5.5, 48, 14 },
				{ 3.0, 110, 20 },
				{ 1.6, 255, 0 }
			};
			for (const auto& layer : layers)
			{
				QPen glow(withAlpha(tokens.accent, qMin(255, layer.alpha + qRound(layer.lift * hover))), layer.width);
				glow.setCapStyle(Qt::RoundCap);
				glow.setJoinStyle(Qt::RoundJoin);
				painter.setPen(glow);
				painter.drawPolyline(segment);
			}

			// The overshoot segment ignites: the same stroke ladder re-drawn
			// in danger, clipped to the glass above the anchor.
			if (state.clipping && zeroClamped > plot.top())
			{
				painter.save();
				painter.setClipRect(QRectF(plot.left(), plot.top(), plot.width(), zeroClamped - plot.top()),
					Qt::IntersectClip);
				const struct { double width; int alpha; } flames[] = {
					{ 6.5, 44 },
					{ 3.2, 130 },
					{ 1.6, 255 }
				};
				for (const auto& flame : flames)
				{
					QPen firePen(withAlpha(tokens.danger, flame.alpha), flame.width);
					firePen.setCapStyle(Qt::RoundCap);
					firePen.setJoinStyle(Qt::RoundJoin);
					painter.setPen(firePen);
					painter.drawPolyline(segment);
				}
				painter.restore();
			}
		}

		// The CLIP flag: a lit danger glass chip (type-badge grammar) at the
		// pane's top right - bloom stroke first, hairline and ink on top.
		if (state.clipping)
		{
			QFont chipFont(tokens.monoFontFamily);
			chipFont.setPointSizeF(7.0);
			chipFont.setWeight(QFont::DemiBold);
			chipFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
			const QFontMetricsF chipMetrics(chipFont);
			const QString clipText = QStringLiteral("CLIP");
			const QRectF chip(plot.right() - chipMetrics.horizontalAdvance(clipText) - 20.0, plot.top() + 6.0,
				chipMetrics.horizontalAdvance(clipText) + 14.0, 16.0);
			painter.setPen(QPen(withAlpha(tokens.danger, 44), 3.0));
			painter.setBrush(withAlpha(tokens.danger, dark ? 38 : 26));
			painter.drawRoundedRect(chip, 8.0, 8.0);
			painter.setPen(QPen(withAlpha(tokens.danger, 150), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(chip, 8.0, 8.0);
			painter.setFont(chipFont);
			painter.setPen(QColor(tokens.danger));
			painter.drawText(chip, Qt::AlignCenter, clipText);
		}

		// Cursor: a vertical light seam pooling at the reading point, the
		// indicator dot on the trace and a lit glass reading chip. The whole
		// group rides state.hover for its entry motion.
		if (state.cursorValid && hover > 0.01)
		{
			painter.save();
			painter.setOpacity(painter.opacity() * hover);

			const double cx = state.cursor.x();
			const double curveY = qBound(plot.top(), state.curveYAtCursor, plot.bottom());
			const double poolAt = qBound(0.05, (curveY - plot.top()) / qMax(1.0, plot.height()), 0.95);
			const auto seam = [&](int alpha) {
				QLinearGradient gradient(cx, plot.top(), cx, plot.bottom());
				gradient.setColorAt(0.0, withAlpha(tokens.accent, 0));
				gradient.setColorAt(poolAt, withAlpha(tokens.accent, alpha));
				gradient.setColorAt(1.0, withAlpha(tokens.accent, 0));
				return gradient;
			};
			// Bloom, mid, core: the insert seam's stroke ladder set upright.
			QPen seamBloom(QBrush(seam(56)), 5.0);
			seamBloom.setCapStyle(Qt::RoundCap);
			painter.setPen(seamBloom);
			painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
			QPen seamMid(QBrush(seam(140)), 2.4);
			seamMid.setCapStyle(Qt::RoundCap);
			painter.setPen(seamMid);
			painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
			QPen seamCore(QBrush(seam(235)), 1.0);
			seamCore.setCapStyle(Qt::RoundCap);
			painter.setPen(seamCore);
			painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));

			// The reading point: the indicator dot (halo + core) on the trace.
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(tokens.accent, 110));
			painter.drawEllipse(QPointF(cx, curveY), 6.0, 6.0);
			painter.setBrush(QColor(tokens.accent));
			painter.drawEllipse(QPointF(cx, curveY), 3.0, 3.0);

			// The reading chip: sunken glass over the pane, accent-lit edge,
			// DM Mono value ink. It follows the dot and flips or clamps to
			// stay on the glass.
			if (!state.cursorText.isEmpty())
			{
				QFont readoutFont(tokens.monoFontFamily);
				readoutFont.setPointSizeF(7.5);
				readoutFont.setWeight(QFont::DemiBold);
				const QFontMetricsF readoutMetrics(readoutFont);
				const QString readout = readoutMetrics.elidedText(state.cursorText, Qt::ElideRight,
					qMax(20.0, plot.width() - 24.0));
				const double chipWidth = readoutMetrics.horizontalAdvance(readout) + 16.0;
				const double chipHeight = 18.0;
				double chipX = cx + 10.0;
				if (chipX + chipWidth > plot.right() - 4.0)
					chipX = cx - 10.0 - chipWidth;
				chipX = qBound(plot.left() + 4.0, chipX, qMax(plot.left() + 4.0, plot.right() - chipWidth - 4.0));
				double chipY = curveY - chipHeight - 8.0;
				if (chipY < plot.top() + 4.0)
					chipY = curveY + 8.0;
				chipY = qBound(plot.top() + 4.0, chipY, qMax(plot.top() + 4.0, plot.bottom() - chipHeight - 4.0));
				const QRectF chip(chipX, chipY, chipWidth, chipHeight);

				painter.setPen(QPen(withAlpha(tokens.accent, 44), 3.0));
				painter.setBrush(withAlpha(tokens.graph, dark ? 222 : 240));
				painter.drawRoundedRect(chip, 8.0, 8.0);
				painter.setPen(QPen(withAlpha(tokens.accent, 130), 1.0));
				painter.setBrush(Qt::NoBrush);
				painter.drawRoundedRect(chip, 8.0, 8.0);
				painter.setFont(readoutFont);
				painter.setPen(QColor(tokens.text));
				painter.drawText(chip, Qt::AlignCenter, readout);
			}
			painter.restore();
		}

		// The pane's edge: a hairline border over a darker inner top edge.
		painter.setClipping(false);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(tokens.border), 1.0));
		painter.drawRoundedRect(frame, 8.0, 8.0);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.fillRect(QRectF(frame.left() + 7.0, frame.top() + 1.0, frame.width() - 14.0, 1.0),
			dark ? QColor(0, 0, 0, 140) : QColor(0, 0, 0, 30));

		painter.restore();
	}

	// The type badge is a lit glass chip: translucent fill with the ink and
	// border in the row's light colour. BiQuad rows resolve their family
	// through the studioBand property the prepareCommandRow hook tagged
	// them with; other commands keep their model colour as quiet ink.
	// Disabled rows switch the chip off.
	BadgeTreatment badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		if (!info.enabled)
		{
			QColor sleeping(tokens.mutedText);
			sleeping.setAlphaF(0.65f);
			return {
				QStringLiteral("QLabel#FilterTypeBadge { color: %1; background-color: transparent; border: 1px solid %2; }")
					.arg(cssRgba(tokens.mutedText, 0.65), cssRgba(tokens.border, 0.55)),
				sleeping
			};
		}

		const QString baseInk = info.type == QStringLiteral("biquad") ? tokens.accent : typeColor;
		QString style = QStringLiteral("QLabel#FilterTypeBadge { color: %1; background-color: %2; border: 1px solid %3; }")
			.arg(baseInk, cssRgba(baseInk, dark ? 0.15 : 0.10), cssRgba(baseInk, dark ? 0.42 : 0.45));
		if (info.type == QStringLiteral("biquad"))
		{
			for (const char* family : studioBandFamilies)
			{
				const QString band = studioBandHex(QLatin1String(family), dark);
				style += QStringLiteral(" QLabel#FilterTypeBadge[studioBand=\"%1\"] { color: %2; background-color: %3; border: 1px solid %4; }")
					.arg(QLatin1String(family), band, cssRgba(band, dark ? 0.15 : 0.10), cssRgba(band, dark ? 0.42 : 0.45));
			}
		}
		const QColor ink = info.type == QStringLiteral("biquad")
			? QColor(studioBandHex(studioBandFamilyForBadgeToken(badgeToken), dark))
			: QColor(typeColor);
		return { style, ink };
	}

	// Tags BiQuad rows with their band family so the QSS attribute
	// selectors (frame hover/selected border, type badge chip) and the paint
	// hooks (knob arcs, signal lamp) all light the row in one colour. The tag
	// follows the type selector live; repolishing re-evaluates the same rules
	// cardFrameStyle/typeBadgeStyle returned at construction.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		if (body == nullptr)
			return;

		if (info.type == QStringLiteral("text") || info.type == QStringLiteral("if")
			|| info.type == QStringLiteral("eval") || info.dynamicLine)
		{
			// Raw text (bare note lines), the If/Eval logic rows and dynamic
			// lines without a dynamic-capable editor host the shared raw
			// editor. FilterCardRow lays a shared inline style on the
			// preview label (inline outranks any sheet rule), and that
			// default speaks a half-radius 4px - illegal under the
			// one-8px-round law - so this hook rewrites it into the sunken
			// data window the reference cards already use. The '>_'
			// engraving already sits in muted mono and stays untouched.
			QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText"));
			if (raw == nullptr)
				return;
			const SkinTokens tokens = SkinManager::instance()->tokens();
			const bool dark = skinIsDark(tokens);
			const QString fill = dark
				? (info.enabled ? QStringLiteral("rgba(6, 9, 20, 0.55)") : QStringLiteral("rgba(6, 9, 20, 0.30)"))
				: (info.enabled ? QStringLiteral("rgba(232, 238, 248, 0.75)") : QStringLiteral("rgba(232, 238, 248, 0.40)"));
			const QString topEdge = dark
				? (info.enabled ? QStringLiteral("rgba(0, 0, 0, 0.55)") : QStringLiteral("rgba(0, 0, 0, 0.30)"))
				: (info.enabled ? QStringLiteral("rgba(0, 0, 0, 0.12)") : QStringLiteral("rgba(0, 0, 0, 0.06)"));
			raw->setStyleSheet(QStringLiteral(
				"QLabel#FilterCardRawText { background: %1; color: %2; border: 1px solid %3;"
				" border-top-color: %4; border-radius: 8px; padding: 6px 10px;"
				" font-family: \"%5\", \"Consolas\", \"Malgun Gothic\", monospace; font-size: 9pt; }")
				.arg(fill,
					info.enabled ? tokens.text : cssRgba(tokens.mutedText, 0.60),
					cssRgba(tokens.border, info.enabled ? 0.90 : 0.55),
					topEdge,
					tokens.monoFontFamily));
			return;
		}

		if (info.type != QStringLiteral("biquad"))
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

	// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).
};
}

ISkin* studioSkin()
{
	static StudioSkin instance;
	return &instance;
}
