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
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Soft ("The iPhone settings screen": macOS System Settings calm) ─────────
// Round, roomy, impossible to fear. Shadows are faked with two background
// value steps plus a very light 1px border; hierarchy comes from size and
// whitespace, never from density. Hover lifts a surface exactly one value
// step. Tiebreaker: when in doubt, remove elements and add whitespace.
//
// The mix/alpha/is-dark vocabulary and the constitution-cited pastel recipe
// (softPastelize) live in the shared SkinPaint.h; the recipe stays Soft-only
// by decree there (differentiation gate).

class SoftSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("soft"); }
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

	// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).

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
		const bool dark = skinIsDark(t);
		const QColor pastel = softPastelize(QColor(typeColor), dark);
		if (!info.enabled)
		{
			const QColor sleeping = mixColor(pastel, QColor(t.background), 0.62);
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

	// Round 3, the trailing add row (shared insertion contract,
	// docs/skins/README.md): "the place where the next card will arrive",
	// answered kindly. The slot is a full-height dashed STADIUM - the skin's
	// established "nothing vouches for this yet" edge (Copy's dashed [+]
	// chip, the virtual channel seats), not a sleeping slot: it stays at the
	// window elevation with a quiet sunken "+" disc waiting at the centre.
	// Hover lifts the whole slot exactly one value step and flips the disc
	// ON in the skin's state grammar (opaque accent pastel + deep warm ink,
	// the toggle-switches-on moment); pressing deepens the pastel one step,
	// the same ladder the ON pills climb. Focus is the constitutional quiet
	// halo, never a hard ring. Whitespace is not economised: one disc, one
	// caption, nothing else.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing);

		const QColor accent(tokens.accent);
		const QColor warmInk(QStringLiteral("#2B251D"));
		const bool lifted = state.hovered || state.pressed;

		QRectF frame = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
		const qreal radius = frame.height() / 2.0;

		// Hover: the slot rises one value step above the window (no shadow -
		// the two-step elevation rule fakes it with the fill + light border).
		if (lifted)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(tokens.surface));
			painter.drawRoundedRect(frame, radius, radius);
		}

		// Keyboard focus: the quiet halo (alpha 90, 3px), not a hard ring.
		if (state.focused)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(frame, radius, radius);
		}

		QPen outline(lifted ? withAlpha(accent, state.pressed ? 210 : 150) : QColor(tokens.border), 1, Qt::DashLine);
		outline.setCapStyle(Qt::RoundCap);
		painter.setPen(outline);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(frame, radius, radius);

		// Centred friendly composition: the "+" disc and the caption.
		QFont font(tokens.fontFamily);
		font.setPointSizeF(10.0);
		font.setWeight(QFont::DemiBold);
		const QFontMetricsF metrics(font);
		const qreal discD = 24.0;
		const qreal gap = 10.0;
		const QString caption = metrics.elidedText(state.label, Qt::ElideRight,
			int(qMax<qreal>(40.0, frame.width() - discD - gap - 48.0)));
		const qreal textW = metrics.horizontalAdvance(caption);
		const qreal left = frame.center().x() - (discD + gap + textW) / 2.0;
		QRectF discRect(left, frame.center().y() - discD / 2.0, discD, discD);

		if (state.pressed)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(mixColor(accent, warmInk, 0.18));
		}
		else if (state.hovered)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(accent);
		}
		else
		{
			painter.setPen(QPen(QColor(tokens.border), 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
		}
		painter.drawEllipse(discRect);

		QPen plusPen(lifted ? warmInk : QColor(tokens.mutedText), 2.4, Qt::SolidLine, Qt::RoundCap);
		painter.setPen(plusPen);
		const QPointF discCenter = discRect.center();
		const qreal arm = discD * 0.21;
		painter.drawLine(QPointF(discCenter.x() - arm, discCenter.y()), QPointF(discCenter.x() + arm, discCenter.y()));
		painter.drawLine(QPointF(discCenter.x(), discCenter.y() - arm), QPointF(discCenter.x(), discCenter.y() + arm));

		painter.setFont(font);
		painter.setPen(lifted ? QColor(tokens.text) : QColor(tokens.mutedText));
		painter.drawText(QRectF(left + discD + gap, frame.top(), textW + 4.0, frame.height()),
			Qt::AlignVCenter | Qt::AlignLeft, caption);
	}

	// Round 3, the first-boundary insertion seam: a pastel pill line led by
	// a round "+" disc. The line is the value-arc pastel (accent mixed one
	// step toward the card), a stadium bar rather than a hairline - soft has
	// no hairline vocabulary - and the disc wears the ON grammar (opaque
	// accent pastel, deep warm ink strokes); pressing deepens the pastel one
	// step. At rest the widget paints nothing (shared contract).
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		if (!state.hovered && !state.pressed)
			return;

		painter.setRenderHint(QPainter::Antialiasing);
		const QColor accent(tokens.accent);
		const QColor warmInk(QStringLiteral("#2B251D"));
		QRectF r(rect);
		const qreal cy = r.center().y();
		const qreal discR = qMin<qreal>(9.0, r.height() / 2.0);
		const qreal discCx = r.left() + discR + 4.0;

		const qreal lineH = qBound<qreal>(3.0, r.height() * 0.5, 5.0);
		QRectF bar(discCx + discR + 6.0, cy - lineH / 2.0,
			r.right() - 4.0 - (discCx + discR + 6.0), lineH);
		painter.setPen(Qt::NoPen);
		painter.setBrush(mixColor(accent, QColor(tokens.card), 0.25));
		painter.drawRoundedRect(bar, lineH / 2.0, lineH / 2.0);

		painter.setBrush(state.pressed ? mixColor(accent, warmInk, 0.18) : accent);
		painter.drawEllipse(QPointF(discCx, cy), discR, discR);

		QPen plusPen(warmInk, qMax<qreal>(1.6, discR * 0.36), Qt::SolidLine, Qt::RoundCap);
		painter.setPen(plusPen);
		const qreal arm = discR * 0.45;
		painter.drawLine(QPointF(discCx - arm, cy), QPointF(discCx + arm, cy));
		painter.drawLine(QPointF(discCx, cy - arm), QPointF(discCx, cy + arm));
	}

	// Round 3 rework, the GraphicEQ response plot: "the response curve you
	// cannot fear". GraphicEQPlotWidget owns the model and every gesture;
	// every pixel here is this skin's own instrument. The ground is the
	// established rounded sunken well (14px round, surfaceSunken face, very
	// light 1px border - elevation faked by the two value steps, never a
	// shadow). The grid keeps only the MAJOR lines, very faint: minor
	// hairlines are exactly the anxious element the tiebreaker removes, and
	// whitespace does their job. The 0 dB line is a soft notch - rounded
	// ends, floating clear of the well walls, the knob's 12-o'clock detent
	// grammar laid flat. The curve imports the bipolar knob's track law:
	// boost above 0 dB strokes the accent pastel, cut below strokes accent2
	// (both softened one step toward the card, the value-arc mix 0.25
	// recipe) over a light pastel wash down to the notch; clipping splits
	// the passes exactly at the zero crossing. Nodes are big round dots
	// (rest 5px) that grow half a pixel on hover and flip ON when selected
	// (opaque pastel fill + the light ring); the keyboard's node wears the
	// quiet halo. Focus on the surface is the constitutional quiet halo
	// hugging the well; a disabled plot is the sleeping slot triple (dashed
	// outline + sunk to the window + muted-ink ghost curve), never an alarm.
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		const QColor card(tokens.card);
		const QColor accent(tokens.accent);
		const QColor accent2(tokens.accent2);
		const QColor muted(tokens.mutedText);
		const QColor border(tokens.border);
		const QColor well = state.enabled ? QColor(tokens.surfaceSunken) : QColor(tokens.background);

		QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
		const qreal wellRound = 14.0;
		QPainterPath wellPath;
		wellPath.addRoundedRect(frame, wellRound, wellRound);

		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(well);
		painter.drawPath(wellPath);

		painter.save();
		painter.setClipPath(wellPath);

		// Axis captions ride the body face in faded ink - the constitution
		// reserves mono for value chips, and these are captions.
		QFont labelFont(tokens.fontFamily);
		labelFont.setPointSizeF(7.5);
		labelFont.setWeight(QFont::DemiBold);
		painter.setFont(labelFont);
		const QColor labelInk = withAlpha(muted, state.enabled ? 210 : 120);

		// Major-only grid, the border sunk most of the way into the well.
		// Straight axis lines stay crisp: antialiasing off. The sleeping slot
		// drops the lines entirely and keeps only the captions - whitespace.
		painter.setRenderHint(QPainter::Antialiasing, false);
		const QColor gridInk = mixColor(border, well, 0.25);
		for (const GraphicEQPlotState::GridLine& line : state.vertical)
		{
			if (!line.major)
				continue;
			const int x = qRound(line.pos);
			if (state.enabled)
			{
				painter.setPen(QPen(gridInk, 1));
				painter.drawLine(x, int(state.plotRect.top()), x, int(state.plotRect.bottom()));
			}
			if (!line.label.isEmpty())
			{
				painter.setPen(labelInk);
				// The window-edge caption (20k) tucks inside the rounding
				// instead of getting sliced by the clip.
				QRect labelRect(x - 24, int(state.plotRect.bottom()) + 3, 48,
					state.rect.bottom() - int(state.plotRect.bottom()) - 3);
				int align = Qt::AlignHCenter;
				if (labelRect.right() > state.rect.right() - 6)
				{
					labelRect.setRight(state.rect.right() - 6);
					align = Qt::AlignRight;
				}
				painter.drawText(labelRect, align | Qt::AlignTop, line.label);
			}
		}
		for (const GraphicEQPlotState::GridLine& line : state.horizontal)
		{
			if (!line.major)
				continue;
			const int y = qRound(line.pos);
			// The 0 dB row is the soft notch drawn below; skip its grid twin.
			if (state.enabled && qAbs(line.pos - state.zeroY) > 1.0)
			{
				painter.setPen(QPen(gridInk, 1));
				painter.drawLine(int(state.plotRect.left()), y, int(state.plotRect.right()), y);
			}
			if (!line.label.isEmpty())
			{
				painter.setPen(labelInk);
				painter.drawText(QRect(state.rect.left() + 2, y - 8,
					int(state.plotRect.left()) - state.rect.left() - 8, 16),
					Qt::AlignRight | Qt::AlignVCenter, line.label);
			}
		}
		painter.setRenderHint(QPainter::Antialiasing, true);

		// The soft 0 dB notch line.
		if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.text), state.enabled ? 110 : 55), 2,
				Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(state.plotRect.left() + 6.0, state.zeroY),
				QPointF(state.plotRect.right() - 6.0, state.zeroY));
		}

		const QColor boost = mixColor(accent, card, 0.25);
		const QColor cut = mixColor(accent2, card, 0.25);
		if (state.curve.size() >= 2)
		{
			if (!state.enabled)
			{
				// Sleeping: the ghost of the response in muted ink, no pastel.
				painter.setPen(QPen(withAlpha(muted, 120), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				painter.setBrush(Qt::NoBrush);
				painter.drawPolyline(state.curve);
			}
			else
			{
				const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
				QPolygonF fillPoly = state.curve;
				fillPoly.append(QPointF(state.curve.last().x(), base));
				fillPoly.prepend(QPointF(state.curve.first().x(), base));

				// Two passes split at the 0 dB seam by clip rects, so the
				// boost/cut colour change lands exactly on the zero crossing
				// (and a frame panned fully past 0 dB gets one whole side).
				const qreal splitY = qBound(frame.top(), qreal(state.zeroY), frame.bottom());
				const QRectF aboveZero(frame.left() - 2.0, frame.top() - 2.0, frame.width() + 4.0, splitY - frame.top() + 2.0);
				const QRectF belowZero(frame.left() - 2.0, splitY, frame.width() + 4.0, frame.bottom() - splitY + 2.0);
				for (int pass = 0; pass < 2; pass++)
				{
					const bool boostPass = pass == 0;
					painter.save();
					painter.setClipRect(boostPass ? aboveZero : belowZero, Qt::IntersectClip);
					painter.setPen(Qt::NoPen);
					painter.setBrush(withAlpha(boostPass ? accent : accent2, 40));
					painter.drawPolygon(fillPoly);
					painter.setPen(QPen(boostPass ? boost : cut, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
					painter.setBrush(Qt::NoBrush);
					painter.drawPolyline(state.curve);
					painter.restore();
				}
			}
		}

		// 15/31-band layouts read as levels on fixed bands: rounded pastel
		// stems grow from the notch, the console silhouette without bar walls.
		if (state.bandLocked && state.enabled)
		{
			const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
			for (const QPointF& node : state.nodePositions)
			{
				painter.setPen(QPen(withAlpha(node.y() > state.zeroY ? accent2 : accent, 90), 4,
					Qt::SolidLine, Qt::RoundCap));
				painter.drawLine(QPointF(node.x(), base), node);
			}
		}

		for (int i = 0; i < state.nodePositions.size(); i++)
		{
			const QPointF& center = state.nodePositions.at(i);
			const bool selected = state.selectedNodes.contains(i);
			const bool hovered = state.hoveredNode == i;
			const QColor side = center.y() > state.zeroY ? accent2 : accent;

			if (!state.enabled)
			{
				painter.setPen(QPen(withAlpha(muted, 120), 1.5));
				painter.setBrush(well);
				painter.drawEllipse(center, 4.0, 4.0);
				continue;
			}

			// Rest 5px, half a pixel more on hover - the calmest "you can
			// grab me" cue; precision lives in the readout strip anyway.
			const double radius = hovered ? 5.5 : 5.0;
			if (selected)
			{
				// ON grammar: opaque pastel fill plus the light ring.
				painter.setPen(QPen(withAlpha(side, 90), 3));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(center, radius + 2.5, radius + 2.5);
				painter.setPen(QPen(well, 1.5));
				painter.setBrush(side);
			}
			else
			{
				// OFF: the quiet elevated face with the side's pastel edge;
				// hover lifts the face exactly one value step.
				painter.setPen(QPen(mixColor(side, card, 0.25), 2));
				painter.setBrush(hovered ? QColor(tokens.cardHover) : card);
			}
			painter.drawEllipse(center, radius, radius);

			// The keyboard's current node announces itself with the quiet
			// halo (selection's own ring already covers a selected one).
			if (state.focusedNode == i && state.focused && !selected)
			{
				painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(center, radius + 4.0, radius + 4.0);
			}
		}

		// Cursor readout: the knob value badge's grammar, a small stadium
		// chip resting in the well's top-right corner.
		if (state.enabled && state.cursorValid && !state.cursorText.isEmpty())
		{
			QFont pillFont(tokens.fontFamily);
			pillFont.setPointSizeF(7.5);
			pillFont.setWeight(QFont::DemiBold);
			const QFontMetricsF pillMetrics(pillFont);
			const qreal pillH = 18.0;
			const qreal pillW = pillMetrics.horizontalAdvance(state.cursorText) + 16.0;
			QRectF pill(state.plotRect.right() - pillW - 6.0, state.plotRect.top() + 6.0, pillW, pillH);
			painter.setPen(QPen(border, 1));
			painter.setBrush(card);
			painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
			painter.setFont(pillFont);
			painter.setPen(QColor(tokens.text));
			painter.drawText(pill, Qt::AlignCenter, state.cursorText);
		}

		painter.restore();

		// The well edge: a very light 1px line awake; asleep it becomes the
		// dashed outline of the sleeping-slot triple.
		QPen edge(border, 1);
		if (!state.enabled)
			edge.setStyle(Qt::DashLine);
		painter.setPen(edge);
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(wellPath);

		// Keyboard focus on the surface itself: the quiet halo hugging the
		// inside of the well, never a hard ring.
		if (state.focused && state.enabled)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
			painter.drawRoundedRect(frame.adjusted(2.0, 2.0, -2.0, -2.0), 12.0, 12.0);
		}
	}

	// Round 3, the plain-text rows (bare note lines and programmatic
	// commands such as If/EndIf/Eval). The raw line IS the row's content
	// here, so it stays readable - but the terminal prompt glyph (">_") is
	// exactly the anxious tech artifact this skin removes (tiebreaker:
	// remove the element, keep the whitespace). The line itself sits in a
	// sunken stadium well with a DASHED edge at full ink - the established
	// "the engine holds this as written, nothing vouches for it" grammar
	// (Copy's [+] chip, the virtual channel seats) - a note, not an alarm.
	// A commented-out row relaxes the well to the sleeping triple (dash +
	// sunk-to-window + muted ink). FilterCardRow laid these styles inline,
	// so QSS cannot reach them; construction time is the hook's moment.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		Q_UNUSED(header);
		if (info.legacyRow || body == nullptr || info.type != QStringLiteral("text"))
			return;

		const SkinTokens t = SkinManager::instance()->tokens();
		if (QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("FilterCardRawGlyph")))
			glyph->setVisible(false);
		if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
		{
			raw->setStyleSheet(QStringLiteral(
				"QLabel#FilterCardRawText { background:%1; color:%2; border:1px dashed %3; border-radius:16px; padding:8px 14px; font-family:\"%4\"; }"
				"QLabel#FilterCardRawText:disabled { background:%5; color:%6; }")
				.arg(t.surfaceSunken, t.text, t.border, t.monoFontFamily, t.background, t.mutedText));
		}
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
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
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
			painter.setPen(QPen(mixColor(QColor(tokens.accent2), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees / 2.0 * 16.0));
			painter.setPen(QPen(mixColor(QColor(tokens.accent), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-spanDegrees / 2.0 * 16.0));
		}
		else
		{
			const QColor trackColor = state.enabled ? mixColor(QColor(tokens.accent), card, 0.80) : withAlpha(border, 110);
			painter.setPen(QPen(trackColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
			painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);
		}

		// Pastel value arc (accent softened one step toward the card colour).
		if (state.enabled)
		{
			const bool cutSide = state.bipolar && ratio < 0.5;
			const QColor valueColor = mixColor(QColor(cutSide ? tokens.accent2 : tokens.accent), card, 0.25);
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
			painter.setPen(QPen(withAlpha(QColor(tokens.text), state.enabled ? 200 : 90), 2.5, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(arcCenter.x(), arcCenter.y() - trackRadius - arcWidth / 2.0 + 0.5),
				QPointF(arcCenter.x(), arcCenter.y() - trackRadius + arcWidth / 2.0 - 0.5));
		}

		// Two-step elevation body: a base disc one value step below the face,
		// then the face one step above with a very light 1px border. Hover
		// lifts the face exactly one value step; no real shadow effects.
		const double faceInset = arcWidth + 2.5;
		QRectF baseRect = knobRect.adjusted(faceInset, faceInset, -faceInset, -faceInset);
		painter.setPen(Qt::NoPen);
		painter.setBrush(mixColor(card, windowBg, 0.55));
		painter.drawEllipse(baseRect);

		QColor faceColor = card;
		if (!state.enabled)
			faceColor = mixColor(card, windowBg, 0.5);
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
		painter.setBrush(state.enabled ? dotColor : withAlpha(muted, 120));
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
			painter.setBrush(state.enabled ? QColor(tokens.surfaceRaised) : mixColor(card, windowBg, 0.5));
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
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/file-new.svg"), mixColor(QColor(tokens.accent), card, 0.15)));
			else if (action->objectName() == QStringLiteral("actionOpen"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/folder-open.svg"), mixColor(QColor(tokens.warning), card, 0.15)));
			else if (action->objectName() == QStringLiteral("actionSave"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/save.svg"), mixColor(QColor(tokens.success), card, 0.15)));
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
