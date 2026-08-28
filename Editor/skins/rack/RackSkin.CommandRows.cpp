/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QWidget>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/FilterCardModel.h"
#include "RackSkinDetail.h"

namespace
{
QString unitLabel(const CommandRowInfo& info)
{
	static const struct { const char* type; const char* label; } table[] = {
		{ "biquad", "FILTER" }, { "graphiceq", "GRAPHIC" }, { "include", "PATCH" },
		{ "vst", "VST" }, { "copy", "ROUTE" }, { "preamp", "PREAMP" },
		{ "channel", "CHANNEL" }, { "device", "DEVICE" }, { "stage", "STAGE" },
		{ "delay", "DELAY" }, { "convolution", "CONV" }, { "loudness", "LOUDNESS" },
		{ "comment", "NOTE" }, { "text", "AUX" }
	};
	for (const auto& entry : table)
		if (info.type == QLatin1String(entry.type))
			return QLatin1String(entry.label);
	return info.command.toUpper().left(8);
}
}

QString RackSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
		// QSS only provides the machined base plate and the hover brightening;
		// the faceplate texture, ears, screws and LEDs are painted on top by
		// paintCardChrome (the sheen overlays are translucent, so
		// the hover state shines through them). The resting border is the dark
		// seam of the rack opening rather than the token border, so stacked
		// units separate physically; focus and selection keep their signal
		// colours.
		const bool dark = skinIsDark(tokens);
		const QString seam = dark ? QStringLiteral("#060809") : QStringLiteral("#8F8268");
		// The engaged frame (selected or focused unit). In dark mode the full
		// amber both popped and glared (maintainer, brightness round 2:
		// "the border colour is the problem"); the judged replacement is a
		// warm neutral - lamps, checked buttons and knob markers keep the
		// accent. Light mode was not judged and keeps its accent frame.
		const QString engaged = dark ? RackSkinDetail::darkEngagedFrame()
			: (info.focused ? tokens.focusRing : tokens.accent);
		const QString borderColor = info.focused || info.selected ? engaged : seam;
		const QString background = info.selected ? tokens.cardSelected : tokens.card;
		return QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
			"QFrame#FilterCardRow:hover { background: %4; }")
			.arg(background, borderColor)
			.arg(tokens.borderRadius)
			.arg(info.selected ? tokens.cardSelected : tokens.cardHover);
	}

QString RackSkin::cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const
{
		// The header strip is part of the painted faceplate; a transparent
		// background lets the brushed metal, ears and LEDs show through.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; }");
	}

void RackSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const
{
		// Reserve the rack-ear zones along the faceplate edges so the painted
		// chrome (screws, LEDs, patchbay jacks, the VST nameplate) never
		// collides with row content. Rows are rebuilt on every skin switch, so
		// this only ever runs while the rack skin is active.
		if (header != nullptr && header->layout() != nullptr)
		{
			const int right = RackSkinDetail::EarWidth + 6
				+ (info.type == QStringLiteral("vst") ? int(RackSkinDetail::NameplateWidth) + 14 : 0);
			header->layout()->setContentsMargins(RackSkinDetail::EarWidth + 6, 4, right, 4);
		}
		// Only the modern card's body stack is inset; body-only consultations
		// (Include/VST editors, legacy rows) already sit inside that stack.
		if (card != nullptr && body != nullptr)
			body->setContentsMargins(RackSkinDetail::EarWidth + 4, 0, RackSkinDetail::EarWidth + 4, 6);

		// Unparsed lines (bare text, programmatic commands like If) are the
		// AUX unit's programming LCD: the as-written line burns in green
		// segments in a dark recessed well, in both finishes - displays never
		// follow the panel finish. The row widget seeds this label with an
		// inline token style QSS cannot beat, so the display law is applied
		// here, and a powered-down unit dims its segments at the same time
		// (rows are rebuilt whenever the line's state changes).
		if (FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine) && body != nullptr)
		{
			if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				const SkinTokens& tk = tokens;
				const bool dark = skinIsDark(tk);
				const QString glass = dark ? QStringLiteral("#0B0F0C") : QStringLiteral("#11150F");
				const QString segments = !info.enabled
					? (dark ? QStringLiteral("#3A6B51") : QStringLiteral("#2F6B4D"))
					: (dark ? QStringLiteral("#86F2BA") : QStringLiteral("#3ED68E"));
				const QString bezel = dark ? QStringLiteral("#050807") : QStringLiteral("#4A4438");
				const QString lowerLip = dark ? QStringLiteral("#39424A") : QStringLiteral("#6B6354");
				raw->setStyleSheet(QStringLiteral(
					"QLabel#FilterCardRawText { background:%1; color:%2;"
					" border:1px solid %3; border-bottom-color:%4; border-radius:2px;"
					" padding:6px 10px; font-family:\"%5\"; font-weight:700; }")
					.arg(glass, segments, bezel, lowerLip, tk.monoFontFamily));
			}
		}
	}

void RackSkin::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	// Blank lines are gaps in the rack, not units: no faceplate.
	if (info.type == QLatin1String("spacer"))
		return;

	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
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
	RackSkinDetail::paintBrushing(painter, r, dark, uint(qHash(info.command)));

	// Rack ears, separated from the panel by a machined groove.
	const QRectF leftEar(r.left(), r.top(), RackSkinDetail::EarWidth, r.height());
	const QRectF rightEar(r.right() - RackSkinDetail::EarWidth, r.top(), RackSkinDetail::EarWidth, r.height());
	const QColor earFill(0, 0, 0, dark ? 52 : 20);
	painter.fillRect(leftEar, earFill);
	painter.fillRect(rightEar, earFill);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
	painter.drawLine(QPointF(leftEar.right(), r.top()), QPointF(leftEar.right(), r.bottom()));
	painter.drawLine(QPointF(rightEar.left(), r.top()), QPointF(rightEar.left(), r.bottom()));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
	painter.drawLine(QPointF(leftEar.right() + 1, r.top()), QPointF(leftEar.right() + 1, r.bottom()));
	painter.drawLine(QPointF(rightEar.left() + 1, r.top()), QPointF(rightEar.left() + 1, r.bottom()));

	// Unit seating and bezel: a dark seam runs just inside the QSS
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
		RackSkinDetail::paintScrew(painter, screws[i], 4.0, qreal((seed + uint(i) * 73u) % 180u), dark);

	// Status LEDs on the left ear: green power LED (lit = line active), amber
	// SELECT LED below it.
	RackSkinDetail::paintLed(painter, QPointF(r.left() + 10, r.top() + 21), 3.0, QColor(tokens.accent2), info.enabled, dark);
	if (r.height() >= 44)
		RackSkinDetail::paintLed(painter, QPointF(r.left() + 10, r.top() + 31.5), 2.4, QColor(tokens.accent), info.selected, dark);

	// Include: patchbay insert jacks on the right ear.
	if (info.type == QLatin1String("include"))
	{
		RackSkinDetail::paintJack(painter, QPointF(r.right() - 10, r.top() + 21), dark);
		if (r.height() >= 46)
			RackSkinDetail::paintJack(painter, QPointF(r.right() - 10, r.top() + 32.5), dark);
	}

	// VST: riveted brass brand nameplate right of the control strip. The
	// header layout reserves this area (see RackSkin::prepareCommandRow).
	if (info.type == QLatin1String("vst") && r.width() >= 320)
	{
		const QRectF plateRect(rightEar.left() - 8 - RackSkinDetail::NameplateWidth,
			r.top() + (tokens.rowHeight - RackSkinDetail::NameplateHeight) / 2.0, RackSkinDetail::NameplateWidth, RackSkinDetail::NameplateHeight);
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
		// The brand carries the loaded ABI once the card posts it (r3
		// judging: the plate is the one place the format should live, not a
		// stamped tag crowding the MODULE caption). Until a module loads,
		// the plate reads the plain family brand.
		const QString brand = info.formatTag.isEmpty() ? QStringLiteral("VST") : info.formatTag;
		// Engraved into the brass itself, so the passes are brass-tinted in
		// both modes rather than following the panel's engraving direction.
		painter.setPen(QColor(255, 240, 200, 160));
		painter.drawText(plateRect.translated(1, 1), Qt::AlignCenter, brand);
		painter.setPen(QColor(0x3A, 0x2A, 0x0C));
		painter.drawText(plateRect, Qt::AlignCenter, brand);

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
		QPainterStateGuard labelState(&painter);
		painter.translate(r.left() + 14.5, r.bottom() - 16);
		painter.rotate(-90);
		painter.setFont(earFont);
		const QRectF textRect(0, -10, r.height() - 64, 20);
		RackSkinDetail::engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, label, withAlpha(QColor(tokens.mutedText), 200), dark);
	}

	// Commented-out line: the whole unit is powered down behind a dim film.
	if (!info.enabled)
		painter.fillPath(plate, dark ? QColor(0, 0, 0, 80) : QColor(255, 252, 244, 120));

	// Keyboard focus: a thin amber line along the inner bezel (UI necessity,
	// kept as small as a hardware unit's rail light).
	if (info.focused)
	{
		// The bezel is the second half of the engaged frame (see
		// cardFrameStyle): the same warm neutral in dark mode.
		painter.setPen(QPen(withAlpha(QColor(dark ? RackSkinDetail::darkEngagedFrame() : tokens.focusRing), 190), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
	}
}

bool RackSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const bool ifFamily = info.type == QStringLiteral("if");
	const bool headRow = ifFamily && info.command == QStringLiteral("if");
	const bool branchRow = ifFamily && (info.command == QStringLiteral("elseif") || info.command == QStringLiteral("else"));
	const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
	const int logic = info.logicDepth;
	if (!headRow && logic <= 0)
		return false;

	const bool dark = skinIsDark(tokens);
	// Lane geometry from the row widget; see CommandRowInfo. The branch/tail
	// rows' extra indent unit is already in laneCount.
	const int h = size.height();
	const int junctionY = 4 + tokens.rowHeight / 2;
	const int indentUnits = info.laneCount;
	const int cardLeft = info.cardLeft;

	// The gutter is machined hardware: the bus casing is the rack opening's
	// dark seam, the core is the amber accent, contact blocks and caps are
	// faceplate metal, and the lamps follow the LED palette (green = engaged,
	// danger = evaluation fault). A de-energized run (a swallowed line, a
	// powered-down unit) dims the core instead of raising an alarm.
	const QColor seam(dark ? QStringLiteral("#060809") : QStringLiteral("#8F8268"));
	const QColor amber(tokens.accent);
	const QColor amberDim = mixColor(amber, QColor(tokens.card), 0.55);
	const QColor lampGreen(tokens.accent2);
	const QColor lampDanger(tokens.danger);
	const QColor metal = dark ? QColor(tokens.card).lighter(125) : mixColor(QColor(tokens.card), QColor(tokens.border), 0.35);
	const QColor metalLight = dark ? QColor(tokens.card).lighter(175) : QColor(Qt::white);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(Qt::NoPen);

	const bool live = info.enabled && !info.lineSkipped;
	const auto bandCenter = [&info](int level) { return info.laneCenter(level); };
	const auto busSegment = [&](int level, int y0, int y1, bool segmentLive) {
		const int cx = bandCenter(level);
		painter.fillRect(QRect(cx - 3, y0, 7, y1 - y0), seam);
		painter.fillRect(QRect(cx - 2, y0, 5, y1 - y0), segmentLive ? amber : amberDim);
		painter.fillRect(QRect(cx - 2, y0, 1, y1 - y0), mixColor(segmentLive ? amber : amberDim, QColor(Qt::white), 0.35));
	};
	const auto tapStub = [&](int level, bool stubLive) {
		const int cx = bandCenter(level);
		painter.fillRect(QRect(cx + 3, junctionY - 2, cardLeft - cx - 3, 4), seam);
		painter.fillRect(QRect(cx + 3, junctionY - 1, cardLeft - cx - 3, 2), stubLive ? amber : amberDim);
	};
	const auto contactBlock = [&](const QRect& block) {
		painter.fillRect(block.adjusted(-1, -1, 1, 1), seam);
		painter.fillRect(block, metal);
		painter.fillRect(QRect(block.left(), block.top(), block.width(), 1), metalLight);
	};
	// The relay/pilot lamp in the panel-LED grammar (paintLed) - a bare
	// glowing disc reads as paint, not hardware. Lit green when the branch
	// is taken; an evaluation fault lights the red service bulb; false,
	// short-circuited and not-yet-analyzed all read as the same dark dome.
	const auto lamp = [&](qreal cx, qreal cy, int state, qreal radius) {
		const bool fault = state == 3;
		painter.setRenderHint(QPainter::Antialiasing, true);
		RackSkinDetail::paintLed(painter, QPointF(cx + 0.5, cy + 0.5), radius, fault ? lampDanger : lampGreen, state == 1 || fault, dark);
		painter.setPen(Qt::NoPen);
		painter.setBrush(Qt::NoBrush);
		painter.setRenderHint(QPainter::Antialiasing, false);
	};
	// Outer channel-group levels (levels below the If lanes) keep a quiet
	// dotted rail so a block inside a Channel scope still shows the group.
	const auto channelRail = [&](int level) {
		painter.setPen(QPen(QColor(tokens.border), 1, Qt::DotLine));
		painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
		painter.setPen(Qt::NoPen);
	};

	const int ifLevels = headRow ? logic : (branchRow || tailRow) ? logic - 1 : logic;
	const int channelLevels = qMax(0, indentUnits - ifLevels - ((branchRow || tailRow) ? 1 : 0));
	for (int level = 0; level < channelLevels; level++)
		channelRail(level);

	if (headRow)
	{
		// The relay unit feeds the lane below; the bus only peeks out of the
		// bottom margin, and the lamp on the stub reports the condition.
		for (int level = channelLevels; level < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic;
		busSegment(own, h - 4, h, info.branchState != 0);
		// A small jewel seated in the mounting seam - the feed point's only
		// visible face under the full-width relay unit.
		lamp(bandCenter(own), h - 2.5, info.branchState, 1.4);
	}
	else if (branchRow)
	{
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic - 1;
		busSegment(own, 0, h, true);
		tapStub(own, info.branchState == 1);
		const int cx = bandCenter(own);
		contactBlock(QRect(cx - 3, junctionY - 4, 7, 9));
		lamp(cx, junctionY + 9, info.branchState, 2.2);
	}
	else if (tailRow)
	{
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		const int own = channelLevels + logic - 1;
		busSegment(own, 0, junctionY, true);
		tapStub(own, false);
		const int cx = bandCenter(own);
		contactBlock(QRect(cx - 3, junctionY - 1, 7, 5));
	}
	else
	{
		// A powered unit: tap stub off the innermost bus with a pilot lamp.
		// A swallowed line de-energizes only its own run; the outer lanes
		// stay live for the rows below.
		for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
			busSegment(level, 0, h, true);
		busSegment(channelLevels + logic - 1, 0, h, live);
		tapStub(channelLevels + logic - 1, live);
		lamp(cardLeft - 6, junctionY, info.lineSkipped ? 0 : (info.enabled ? 1 : -1), 2.0);
	}
	return true;
}

bool RackSkin::logicSiblingsIndentAsMembers() const
{
		return true;
	}
