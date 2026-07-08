/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Soft skin, split out of Skins.cpp (audit #109 F005). This is a verbatim
// move of the helpers and the class; behaviour is unchanged. The file-scope
// instance is exposed through softSkin() so Skins::all() can assemble the
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
#include "Editor/skins/cards/SoftReferenceCardView.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"
#include "SkinSupport.h"
#include "SkinThemeData.h"

namespace
{
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
		return SkinThemeData::qssResource(id(), dark);
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

	// AR2 (#97): the reference rows answer in the consumer-settings grammar -
	// a pastel monogram tile leading a two-line identity (name + friendly
	// caption), IR facts as pastel stadium chips, and a calm accent Locate
	// pill as the recovery entry (skins/cards/SoftReferenceCardView.cpp).
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new SoftReferenceCardView(kind, parent);
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

	// The token table lives in SkinThemeData (shared with satellite tools);
	// this class keeps only behaviour. The pastel-shelf rationale moved with
	// the values.
	SkinTokens tokens(bool dark) const override
	{
		return SkinThemeData::tokens(id(), dark);
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

	// The badge pictogram's ink (feedback round 2): the deep warm ink on the
	// pastel chip - white strokes on a pastel are exactly the low-contrast
	// anxiety this skin removes. A sleeping chip relaxes to the muted ink.
	QColor typeBadgeInk(const CommandRowInfo& info, const QString&, const QString&, const SkinTokens& t) const override
	{
		return info.enabled ? QColor(QStringLiteral("#2B251D")) : QColor(t.mutedText);
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
}

ISkin* softSkin()
{
	static SoftSkin instance;
	return &instance;
}
