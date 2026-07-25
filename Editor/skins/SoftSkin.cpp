/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Soft skin. Constitution: docs/skins/soft.md. The file-scope instance is
// exposed through softSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "Skins.h"

#include <QAction>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QIcon>
#include <QCoreApplication>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/pickers/SoftFilterPicker.h"
#include "Editor/skins/cards/SoftReferenceCardView.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Soft ("The iPhone settings screen": macOS System Settings calm) ─────────
// The mix/alpha/is-dark vocabulary and the pastel recipe (softPastelize)
// live in the shared SkinPaint.h; the recipe stays Soft-only by decree
// there (differentiation gate).

// The friendly-sentence grammar of the dynamic commands. Only a right-hand
// side this simple may enter a sentence: a quoted string (the quotes come
// off), a number, or true/false. Everything else counts as complex and keeps
// the summary as written - an honest fallback, the dashed raw well in the
// body already holds the source.
QString softSentenceLiteral(const QString& raw)
{
	const QString value = raw.trimmed();
	if (value.size() >= 2)
	{
		const QChar quote = value.at(0);
		if ((quote == QLatin1Char('"') || quote == QLatin1Char('\'')) && value.endsWith(quote))
		{
			const QString inner = value.mid(1, value.size() - 2);
			if (!inner.contains(QLatin1Char('"')) && !inner.contains(QLatin1Char('\'')))
				return inner;
			return QString();
		}
	}
	static const QRegularExpression simple(QStringLiteral("^([-+]?[0-9]+(\\.[0-9]+)?|true|false)$"),
		QRegularExpression::CaseInsensitiveOption);
	return simple.match(value).hasMatch() ? value : QString();
}

// Retells a simple If/ElseIf comparison (identifier op literal), an Else /
// EndIf marker or a simple Eval assignment as the sentence a settings app
// would dare to show. Returns an empty string when the line is not that
// simple, so the caller leaves the as-written summary alone.
QString softFriendlySentence(const QString& command, const QString& asWritten)
{
	// Else/EndIf carry no expression (the engine ignores text after their
	// colon), so their sentence stands alone. When someone did write
	// something there, keeping it visible is the honest reading.
	if (command == QStringLiteral("else"))
		return asWritten.trimmed().isEmpty() ? QCoreApplication::translate("SoftSkin", "Otherwise") : QString();
	if (command == QStringLiteral("endif"))
		return asWritten.trimmed().isEmpty() ? QCoreApplication::translate("SoftSkin", "End of the rule") : QString();

	if (command == QStringLiteral("eval"))
	{
		// A simple assignment ("x = 5") becomes "Set x to 5"; a computed
		// expression stays as written.
		static const QRegularExpression assignment(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\s*=(?!=)\\s*(.+)$"));
		const QRegularExpressionMatch match = assignment.match(asWritten.trimmed());
		const QString value = match.hasMatch() ? softSentenceLiteral(match.captured(2)) : QString();
		if (value.isEmpty())
			return QString();
		return QCoreApplication::translate("SoftSkin", "Set %1 to %2").arg(match.captured(1), value);
	}

	if (command != QStringLiteral("if") && command != QStringLiteral("elseif"))
		return QString();

	static const QRegularExpression comparison(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)\\s*(==|!=|<=|>=|<|>)\\s*(.+)$"));
	const QRegularExpressionMatch match = comparison.match(asWritten.trimmed());
	const QString value = match.hasMatch() ? softSentenceLiteral(match.captured(3)) : QString();
	if (value.isEmpty())
		return QString();

	// One complete sentence per operator, not an operator phrase slotted
	// into a shared "If %1 is %2" frame: particles and word order change
	// with the comparison in languages like Korean, so only the whole
	// sentence can be translated.
	const QString op = match.captured(2);
	QString sentence;
	if (command == QStringLiteral("if"))
	{
		if (op == QStringLiteral("=="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is %2");
		else if (op == QStringLiteral("!="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is not %2");
		else if (op == QStringLiteral(">="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is at least %2");
		else if (op == QStringLiteral(">"))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is more than %2");
		else if (op == QStringLiteral("<="))
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is at most %2");
		else
			sentence = QCoreApplication::translate("SoftSkin", "If %1 is less than %2");
	}
	else
	{
		if (op == QStringLiteral("=="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is %2");
		else if (op == QStringLiteral("!="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is not %2");
		else if (op == QStringLiteral(">="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is at least %2");
		else if (op == QStringLiteral(">"))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is more than %2");
		else if (op == QStringLiteral("<="))
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is at most %2");
		else
			sentence = QCoreApplication::translate("SoftSkin", "Otherwise, if %1 is less than %2");
	}
	return sentence.arg(match.captured(1), value);
}

class SoftSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("soft"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static BlockChipRoutingRenderer renderer;
		return &renderer;
	}

	// A rounded menu card picker (skins/pickers/SoftFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new SoftFilterPickerView(parent);
	}

	// The reference rows in the consumer-settings grammar
	// (skins/cards/SoftReferenceCardView.cpp).
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

	// The row's type badge wears the picker's pastel grammar instead of the
	// shared saturated pill. The ink is a deep warm neutral on the pastel
	// chip - white text on a pastel is exactly the kind of low-contrast
	// anxiety this skin removes. A sleeping (commented-out) row sinks its
	// chip toward the window background.
	BadgeTreatment badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& t) const override
	{
		Q_UNUSED(badgeToken);
		const bool dark = skinIsDark(t);
		const QColor pastel = softPastelize(QColor(typeColor), dark);
		if (!info.enabled)
		{
			const QColor sleeping = mixColor(pastel, QColor(t.background), 0.62);
			return {
				QStringLiteral("color:%1; border-color:transparent; background-color:%2;")
					.arg(t.mutedText, sleeping.name()),
				QColor(t.mutedText)
			};
		}
		return {
			QStringLiteral("color:#2B251D; border-color:transparent; background-color:%1;")
				.arg(pastel.name()),
			QColor(QStringLiteral("#2B251D"))
		};
	}

	// The trailing add row (shared insertion contract,
	// docs/skins/README.md): a full-height dashed stadium slot - the
	// "nothing vouches for this yet" edge, not a sleeping slot - with a
	// quiet sunken "+" disc waiting at the centre.
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

	// The first-boundary insertion seam: a pastel pill line led by a round
	// "+" disc. At rest the widget paints nothing (shared contract).
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

	// The GraphicEQ response plot: "the response curve you cannot fear".
	// GraphicEQPlotWidget owns the model and every gesture; every pixel
	// here is this skin's own instrument.
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

	// The analysis dock's response graph: "the friendly response landscape".
	// EqGraphView owns the sampling, the axis fit and the cursor; every
	// pixel here is the GraphicEQ instrument's family answer, adapted to a
	// wide always-on monitoring readout. The response draws as TERRAIN:
	// opaque pastel masses in the ON-fill grammar - cut valleys in accent,
	// boost hills in success, warming to the warning pastel the moment the
	// config can clip (state.clipping), named by an "Over 0 dB" chip.
	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override
	{
		const QColor accent(tokens.accent);
		const QColor muted(tokens.mutedText);
		const QColor border(tokens.border);
		const QColor well(tokens.surfaceSunken);
		const QColor warmInk(QStringLiteral("#2B251D"));

		QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
		const qreal wellRound = 14.0;
		QPainterPath wellPath;
		wellPath.addRoundedRect(frame, wellRound, wellRound);

		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::TextAntialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(well);
		painter.drawPath(wellPath);

		painter.save();
		painter.setClipPath(wellPath);

		// Axis captions ride the body face in faded ink, exactly like the
		// GraphicEQ plot (the constitution reserves mono for value chips).
		QFont labelFont(tokens.fontFamily);
		labelFont.setPointSizeF(7.5);
		labelFont.setWeight(QFont::DemiBold);
		painter.setFont(labelFont);
		const QColor labelInk = withAlpha(muted, 210);

		// Major-only grid, the border sunk most of the way into the well;
		// straight lines stay crisp with antialiasing off. The horizontal
		// majors' only member is the zero row, which the soft notch draws
		// itself, so only the frequency decades remain - whitespace does the
		// rest (tiebreaker).
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(mixColor(border, well, 0.25), 1));
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			if (line.major)
				painter.drawLine(qRound(line.pos), int(state.plotRect.top()), qRound(line.pos), int(state.plotRect.bottom()));
		}
		painter.setRenderHint(QPainter::Antialiasing, true);

		// The response terrain: opaque pastel masses split at the ground line
		// by clip rects, so the semantic colour change lands exactly on the
		// zero crossing (the GraphicEQ instrument's seam trick). Each pass
		// lays the mass, then its warm-ink stroke on the terrain edge. One
		// landscape per piece of the response: where the metric has no reading
		// the ground simply ends, because a mass carried across that gap would
		// show a hill nobody measured.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;

			const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
			QPolygonF terrain = segment;
			terrain.append(QPointF(segment.last().x(), base));
			terrain.prepend(QPointF(segment.first().x(), base));

			const QColor overFill(state.clipping ? tokens.warning : tokens.success);
			const qreal splitY = qBound(frame.top(), qreal(state.zeroY), frame.bottom());
			const QRectF aboveZero(frame.left() - 2.0, frame.top() - 2.0, frame.width() + 4.0, splitY - frame.top() + 2.0);
			const QRectF belowZero(frame.left() - 2.0, splitY, frame.width() + 4.0, frame.bottom() - splitY + 2.0);
			for (int pass = 0; pass < 2; pass++)
			{
				const bool overshootPass = pass == 0;
				const QColor side = overshootPass ? overFill : accent;
				painter.save();
				painter.setClipRect(overshootPass ? aboveZero : belowZero, Qt::IntersectClip);
				painter.setPen(Qt::NoPen);
				painter.setBrush(side);
				painter.drawPolygon(terrain);
				painter.setPen(QPen(mixColor(side, warmInk, 0.40), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				painter.setBrush(Qt::NoBrush);
				painter.drawPolyline(segment);
				painter.restore();
			}
		}

		// The calm ground line: the soft zero notch, rounded ends floating
		// clear of the well walls, laid over the masses so the ground level
		// reads through the hills. Drawn only while zero is really inside the
		// fitted range - a group delay that never goes negative would have its
		// ground pressed flat against the well floor, which is a wall, not a
		// landmark.
		if (state.zeroVisible)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.text), 110), 2, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(state.plotRect.left() + 6.0, state.zeroY),
				QPointF(state.plotRect.right() - 6.0, state.zeroY));
		}

		// The frequency axis speaks: the decade figures plus the 20/20k
		// endpoints anchoring the range; the in-between ticks stay
		// whitespace. Edge captions tuck inside the rounding.
		painter.setPen(labelInk);
		for (int i = 0; i < state.vertical.size(); i++)
		{
			const AnalysisGraphState::GridLine& line = state.vertical.at(i);
			if (line.label.isEmpty() || (!line.major && i != 0 && i != state.vertical.size() - 1))
				continue;
			QRect labelRect(qRound(line.pos) - 24, int(state.plotRect.bottom()) + 2, 48, 12);
			int align = Qt::AlignHCenter;
			if (labelRect.right() > state.rect.right() - 8)
			{
				labelRect.setRight(state.rect.right() - 8);
				align = Qt::AlignRight;
			}
			if (labelRect.left() < state.rect.left() + 8)
			{
				labelRect.setLeft(state.rect.left() + 8);
				align = Qt::AlignLeft;
			}
			painter.drawText(labelRect, align | Qt::AlignTop, line.label);
		}

		// The value figures rest just above their (unpainted) rows along the
		// left edge, thinned to a calm cadence when the fitted range packs
		// the rows tighter than a caption, anchored at the zero ground so
		// the kept figures stay symmetric around it. They arrive already
		// worded for whichever metric is showing, so nothing here spells a
		// unit.
		int groundIndex = 0;
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			if (state.horizontal.at(i).major)
			{
				groundIndex = i;
				break;
			}
		}
		qreal rowGap = 0.0;
		if (state.horizontal.size() >= 2)
			rowGap = qAbs(state.horizontal.at(1).pos - state.horizontal.at(0).pos);
		const int labelStride = rowGap > 0.5 ? qMax(1, qCeil(16.0 / rowGap)) : 1;
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
			if (line.label.isEmpty() || qAbs(i - groundIndex) % labelStride != 0)
				continue;
			painter.drawText(QRectF(state.plotRect.left() + 6.0, line.pos - 15.0, 48.0, 12.0),
				Qt::AlignLeft | Qt::AlignVCenter, line.label);
		}

		// The footer caption stays a caption: channel and sample rate in the
		// same friendly ink, centred under the axis row. Localized data,
		// drawn as-is.
		if (!state.channelText.isEmpty())
		{
			const QFontMetrics footerMetrics(labelFont);
			painter.drawText(QRectF(state.plotRect.left(), state.rect.bottom() - 14.0, state.plotRect.width(), 13.0),
				Qt::AlignHCenter | Qt::AlignVCenter,
				footerMetrics.elidedText(state.channelText, Qt::ElideRight, int(state.plotRect.width())));
		}

		// The clipping notice: the overshoot terrain has already warmed to
		// the warning pastel; a stadium chip names it and - because Soft's
		// audience may not know that exceeding 0 dB audibly damages the
		// sound - a plain-language warning sentence follows the chip. No
		// jargon (never "clipping"), localized, and bold enough to matter.
		if (state.clipping)
		{
			const QString clipText = QStringLiteral("Over 0 dB");
			const QFontMetrics chipMetrics(labelFont);
			const qreal chipH = 18.0;
			const qreal chipW = chipMetrics.horizontalAdvance(clipText) + 16.0;
			const QRectF chip(state.plotRect.left() + 8.0, state.plotRect.top() + 6.0, chipW, chipH);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(tokens.warning));
			painter.drawRoundedRect(chip, chipH / 2.0, chipH / 2.0);
			painter.setPen(warmInk);
			painter.drawText(chip, Qt::AlignCenter, clipText);

			const QString advice = QCoreApplication::translate("SoftSkin",
				"Sound may distort - keep it below 0 dB");
			QFont adviceFont(labelFont);
			adviceFont.setWeight(QFont::DemiBold);
			const QFontMetrics adviceMetrics(adviceFont);
			const QRectF adviceRect(chip.right() + 8.0, chip.top(),
				qMax(0.0, state.plotRect.right() - chip.right() - 16.0), chipH);
			if (adviceRect.width() >= 60.0)
			{
				painter.setFont(adviceFont);
				painter.setPen(mixColor(QColor(tokens.warning), warmInk, 0.55));
				painter.drawText(adviceRect, Qt::AlignLeft | Qt::AlignVCenter,
					adviceMetrics.elidedText(advice, Qt::ElideRight, int(adviceRect.width())));
				painter.setFont(labelFont);
			}
		}

		// The cursor: a soft vertical notch guide (the detent grammar stood
		// upright), a rounded lens dot sitting on the response in its side's
		// pastel, and the readout as an ON-pastel stadium pill in the well's
		// top-right corner. The hover progress floats the whole group in,
		// the pill drifting down to its resting spot.
		const double entry = qBound(0.0, state.hover, 1.0);
		if (state.cursorValid && entry > 0.01)
		{
			painter.save();
			painter.setOpacity(entry);

			painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 2, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(state.cursor.x(), state.plotRect.top() + 6.0),
				QPointF(state.cursor.x(), state.plotRect.bottom() - 6.0));

			// The lens: an elevated card face wearing its side's warm-ink
			// ring under a quiet text-ink halo, so it reads both on a
			// terrain mass of the same pastel and on the bare well.
			const bool overshoot = state.curveYAtCursor < state.zeroY - 0.5;
			const QColor lensSide = overshoot ? QColor(state.clipping ? tokens.warning : tokens.success) : accent;
			painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 7.5, 7.5);
			painter.setPen(QPen(mixColor(lensSide, warmInk, 0.40), 2));
			painter.setBrush(QColor(tokens.card));
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 5.0, 5.0);

			if (!state.cursorText.isEmpty())
			{
				const QFontMetrics pillMetrics(labelFont);
				const qreal pillH = 18.0;
				const qreal pillW = qMin<qreal>(pillMetrics.horizontalAdvance(state.cursorText) + 16.0,
						state.plotRect.width() - 12.0);
				const QRectF pill(state.plotRect.right() - pillW - 6.0,
					state.plotRect.top() + 6.0 - (1.0 - entry) * 8.0, pillW, pillH);
				painter.setPen(Qt::NoPen);
				painter.setBrush(accent);
				painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
				painter.setPen(warmInk);
				painter.drawText(pill, Qt::AlignCenter,
					pillMetrics.elidedText(state.cursorText, Qt::ElideRight, int(pillW - 12.0)));
			}
			painter.restore();
		}

		painter.restore();

		// The well edge: the very light 1px line of the two-step elevation.
		painter.setPen(QPen(border, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(wellPath);
	}

	// The plain-text rows (bare note lines and programmatic commands such
	// as If/EndIf/Eval). FilterCardRow lays these styles inline, so QSS
	// cannot reach them; construction time is the hook's moment.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		const bool rawBodyRow = info.type == QStringLiteral("text")
			|| info.type == QStringLiteral("if") || info.type == QStringLiteral("eval")
			|| info.dynamicLine;
		if (info.legacyRow || !rawBodyRow)
			return;

		// Sentence conditions: a simple If/Eval line is retold in the header
		// as a friendly body-typeface sentence ("If device is Speakers",
		// "Otherwise", "Set x to 5").
		// The rewrite is queued because the row constructor
		// calls rebuildSummary() right after this hook, which would restore
		// the as-written summary immediately; the queued call lands once the
		// row has settled (the gallery's processEvents() delivers it too).
		// Known limit: a later rebuildSummary() (raw edit, row reshuffle)
		// restores the as-written text until the row is rebuilt.
		if (header != nullptr && info.type != QStringLiteral("text") && !info.dynamicLine)
		{
			if (QLabel* summary = header->findChild<QLabel*>(QStringLiteral("FilterCardSummary")))
			{
				const QString command = info.command;
				QMetaObject::invokeMethod(summary, [summary, command]() {
					const QString asWritten = summary->text();
					const QString sentence = softFriendlySentence(command, asWritten);
					if (sentence.isEmpty() || sentence == asWritten)
						return;
					// The as-written line stays reachable before the body is
					// ever expanded: the sentence's tooltip answers with it.
					if (!asWritten.trimmed().isEmpty())
						summary->setToolTip(asWritten);
					summary->setText(sentence);
				}, Qt::QueuedConnection);
			}
		}

		if (body == nullptr)
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

	// The If block is held in a pastel arm: one quiet rounded bar per scope
	// level in the gutter, born under the If sentence and closed with its
	// cap on the EndIf row. Level math: for members the if-lanes are the
	// innermost logicDepth bands after the depth-logicDepth channel bands.
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool ifFamily = info.type == QStringLiteral("if");
		const bool headRow = ifFamily && info.command == QStringLiteral("if");
		const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
		const int logic = info.logicDepth;
		if (!headRow && logic <= 0)
			return false;

		const QColor card(tokens.card);
		const QColor accent(tokens.accent);
		// Live scope: the value-arc pastel. Sleeping stretch: the bipolar
		// track's far mix - present and quiet, one step off the ground.
		const QColor arm = mixColor(accent, card, 0.25);
		const QColor armResting = mixColor(accent, card, 0.78);

		const int unit = tokens.channelGroupIndent;
		const int h = size.height();
		// Branch/tail rows mount with the members, one indent unit past
		// their head (logicSiblingsIndentAsMembers), so the arm passes them
		// instead of dying behind their full-width faces. The card edge
		// follows the same rule.
		const int indentUnits = (ifFamily && !headRow) ? info.depth + 1 : info.depth;
		const int channelLevels = qMax(0, indentUnits - logic);
		const bool resting = !info.enabled || info.lineSkipped;

		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);

		// An enclosing Channel group keeps its constitutional SoftShadow
		// band. The hook sees no per-row type colour (the shared rail tints
		// by it), so the shade leans on the border token - quieter, same
		// silhouette.
		if (channelLevels > 0)
		{
			const QRectF band(8 + (channelLevels - 1) * unit, 0, unit, h);
			QLinearGradient shade(band.left(), 0, band.right(), 0);
			shade.setColorAt(0, withAlpha(QColor(tokens.border), 110));
			shade.setColorAt(1, withAlpha(QColor(tokens.border), 0));
			painter.fillRect(band, shade);
		}

		// One bar per scope, centred in its indent band. Straight runs
		// overshoot the row's clip so neighbouring rows tile into one
		// continuous arm; only the arm's first and last row show a cap.
		const auto laneX = [&](int level) {
			return 8.0 + level * unit + unit / 2.0 - 2.0;
		};
		const auto runBar = [&](int level, const QColor& color) {
			painter.setBrush(color);
			painter.drawRoundedRect(QRectF(laneX(level), -4.0, 4.0, h + 8.0), 2.0, 2.0);
		};

		// Outer scopes hold straight through every row of an inner block.
		const int ownLevel = channelLevels + logic - (headRow ? 0 : 1);
		for (int level = channelLevels; level < ownLevel; level++)
			runBar(level, arm);

		if (headRow)
		{
			// The arm begins BELOW the sentence: a rounded fingertip peeking
			// out of the row's bottom margin, which the first member row
			// carries on. A false condition relaxes the fingertip along with
			// the sleeping members it announces (branchState is fresh at
			// paint time by contract).
			painter.setBrush(resting || info.branchState == 0 ? armResting : arm);
			painter.drawRoundedRect(QRectF(laneX(ownLevel), h - 4.0, 4.0, 8.0), 2.0, 2.0);
		}
		else if (tailRow)
		{
			// The arm ends here: the bar falls to the row's centre line and
			// closes with its stadium cap.
			const qreal capY = 4.0 + tokens.rowHeight / 2.0;
			painter.setBrush(resting ? armResting : arm);
			painter.drawRoundedRect(QRectF(laneX(ownLevel), -4.0, 4.0, capY + 4.0), 2.0, 2.0);
		}
		else
		{
			// Members and branch rows: the arm passes at full height; a
			// swallowed line relaxes only its own stretch.
			runBar(ownLevel, resting ? armResting : arm);
		}
		return true;
	}

	// ElseIf/Else/EndIf mount one indent unit past their head, with the
	// members, so the pastel arm passes them instead of dying behind their
	// full-width faces.
	bool logicSiblingsIndentAsMembers() const override
	{
		return true;
	}

	// "A handle you cannot fumble." The largest knob of the five skins:
	// two-step elevation body, rounded dot indicator, value in a rounded
	// badge below, always-visible pastel track ring.
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

		// Always-visible pastel track ring. Unipolar travel wears one
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

		// The 0 dB detent is a soft rounded tick crossing the track ring
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
		// hover and again while dragging, the calmest possible "I am held"
		// cue. The dot is large enough that the position reads from across
		// the row, and on a bipolar knob it takes the colour of the side it
		// sits on (accent boost, accent2 cut).
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
			else if (action->objectName() == QStringLiteral("actionUndo"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/undo.svg"), mixColor(QColor(tokens.accent2), card, 0.15)));
			else if (action->objectName() == QStringLiteral("actionRedo"))
				action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/redo.svg"), mixColor(QColor(tokens.accent2), card, 0.15)));
		}
	}

	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override
	{
		if (dialog == nullptr)
			return;

		// The toolbar's pastel tiles carried into the dialog's navigation
		// row, with the same semantic tints: the movement pair rides
		// accent2 like undo/redo, the folder pair keeps actionOpen's warm
		// tint and actionNew's accent, and the view toggles stay muted so
		// they read as mode switches, not actions.
		const QColor card(tokens.card);
		// The default initializers keep cppcheck's uninitMemberVarNoCtor happy:
		// the QColor member gives this aggregate a non-trivial flavor its
		// heuristic mistakes for a constructor-less class.
		const struct { const char* name = nullptr; const char* resource = nullptr; QColor tile; } buttons[] = {
			{ "backButton", ":/icons/modern/nav-back.svg", mixColor(QColor(tokens.accent2), card, 0.15) },
			{ "forwardButton", ":/icons/modern/nav-forward.svg", mixColor(QColor(tokens.accent2), card, 0.15) },
			{ "toParentButton", ":/icons/modern/folder-up.svg", mixColor(QColor(tokens.warning), card, 0.15) },
			{ "newFolderButton", ":/icons/modern/folder-new.svg", mixColor(QColor(tokens.accent), card, 0.15) },
			{ "listModeButton", ":/icons/modern/view-list.svg", mixColor(QColor(tokens.mutedText), card, 0.15) },
			{ "detailModeButton", ":/icons/modern/view-detail.svg", mixColor(QColor(tokens.mutedText), card, 0.15) },
		};
		for (const auto& button : buttons)
		{
			QToolButton* toolButton = dialog->findChild<QToolButton*>(QLatin1String(button.name));
			if (toolButton != nullptr)
			{
				toolButton->setIcon(softTileIcon(QLatin1String(button.resource), button.tile));
				toolButton->setIconSize(GUIHelper::scale(QSize(22, 22)));
			}
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
			// The whole tile fades when the action is disabled, the shared
			// disabled-glyph recipe (undo/redo on an empty history).
			icon.addPixmap(GUIHelper::fadedPixmap(pixmap), QIcon::Disabled);
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
