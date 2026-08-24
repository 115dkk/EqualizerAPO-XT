/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"
#include "StudioBandColor.h"

#include <QComboBox>
#include <QDial>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QStyle>
#include <QWidget>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/widgets/FilterCardModel.h"

using namespace StudioBandColors;

namespace
{
const char* const studioBandFamilies[] = { "peak", "shelf", "pass", "notch" };
}

QString StudioSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
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

QString StudioSkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
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

void StudioSkin::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	if (!info.enabled || info.type == QStringLiteral("spacer"))
		return;

	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
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
		// analysis run and only paint time sees fresh values. The span
		// runs from the summary label to the header's right edge (the
		// label itself is only as wide as its text), so a long expression
		// keeps the right of way and the value yields.
		if (info.type == QStringLiteral("eval") && !info.evalText.isEmpty() && !info.valueError
			&& painter.device() != nullptr && painter.device()->devType() == QInternal::Widget)
		{
			const QWidget* frame = static_cast<const QWidget*>(painter.device());
			const QLabel* summary = frame->findChild<QLabel*>(QStringLiteral("FilterCardSummary"));
			const QWidget* header = summary != nullptr ? summary->parentWidget() : nullptr;
			if (summary != nullptr && header != nullptr && summary->isVisible())
			{
				const QPoint summaryOrigin = summary->mapTo(frame, QPoint(0, 0));
				const int headerRight = header->mapTo(frame, QPoint(header->width(), 0)).x()
					- (header->layout() != nullptr ? header->layout()->contentsMargins().right() : 8);
				const QRectF span(summaryOrigin, QSizeF(headerRight - summaryOrigin.x(), summary->height()));
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
}

bool StudioSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool ifFamily = info.type == QStringLiteral("if");
	const bool headRow = ifFamily && info.command == QStringLiteral("if");
	const bool branchRow = ifFamily && (info.command == QStringLiteral("elseif") || info.command == QStringLiteral("else"));
	const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
	const int logic = info.logicDepth;
	if (!headRow && logic <= 0)
		return false;

	// Lane geometry from the row widget; see CommandRowInfo. The branch/tail
	// rows' extra indent unit is already in laneCount.
	const int unit = info.laneUnit;
	const double h = size.height();
	const double junctionY = 4.0 + tokens.rowHeight / 2.0;
	const int indentUnits = info.laneCount;

	const QColor beam(tokens.accent);
	const bool live = info.enabled && !info.lineSkipped;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);

	const auto bandCenter = [&info](int level) { return double(info.laneCenter(level)); };

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
	return true;
}

bool StudioSkin::logicSiblingsIndentAsMembers() const
{
	return true;
}

BadgeTreatment StudioSkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
	const QString& badgeToken, const SkinTokens& tokens) const
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

void StudioSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
	const SkinTokens& tokens) const
{
	if (body == nullptr)
		return;

	if (FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine))
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
