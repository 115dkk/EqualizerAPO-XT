/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Rack skin. Constitution: docs/skins/rack.md. The file-scope instance is
// exposed through rackSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "Skins.h"

#include <QFileDialog>
#include <QFontMetricsF>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "Editor/SkinManager.h"
#include "Editor/skins/RackChrome.h"
#include "Editor/skins/cards/RackReferenceCardView.h"
#include "Editor/skins/pickers/RackFilterPicker.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"
#include "SkinPaint.h"
#include "SkinSupport.h"
#include "SkinFileIcons.h"

namespace
{
// File-dialog pictograms in Rack's language: physical objects on the shelf
// behind the machine (round-2 verdict: "보다 스큐어모피즘하게"). A manila
// folder, paper sheets with a real folded corner, a metal drive slab - each
// modelled with a gradient, an outline and a catch-light, and each keeping
// its material colour in both modes the way RackChrome's hardware does
// (physical objects do not follow the panel finish; mode literals are the
// rack constitution's licensed exception).
class RackFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor amber(tokens.accent);
		return paintedIcon([glyph, amber](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			const qreal outlineWidth = qMax(1.0, s * 0.055);

			// Paper sheet with a turned corner; detail() adds the per-type
			// content on the page.
			const auto paperSheet = [&](const std::function<void()>& detail) {
				QPainterPath sheet;
				sheet.moveTo(s * 0.24, s * 0.12);
				sheet.lineTo(s * 0.62, s * 0.12);
				sheet.lineTo(s * 0.76, s * 0.26);
				sheet.lineTo(s * 0.76, s * 0.88);
				sheet.lineTo(s * 0.24, s * 0.88);
				sheet.closeSubpath();
				QLinearGradient paper(0, s * 0.12, 0, s * 0.88);
				paper.setColorAt(0.0, QColor(0xF7, 0xF4, 0xEA));
				paper.setColorAt(1.0, QColor(0xE3, 0xDF, 0xD1));
				painter.setPen(QPen(QColor(0x8A, 0x86, 0x78), outlineWidth));
				painter.setBrush(paper);
				painter.drawPath(sheet);
				// The turned corner: a shaded triangle with its own crease.
				QPainterPath fold;
				fold.moveTo(s * 0.62, s * 0.12);
				fold.lineTo(s * 0.62, s * 0.26);
				fold.lineTo(s * 0.76, s * 0.26);
				fold.closeSubpath();
				painter.setBrush(QColor(0xCD, 0xC8, 0xB6));
				painter.drawPath(fold);
				detail();
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				// Manila folder: tab behind, warm body, worklight on the top
				// edge. Same manila in dark and light - cardboard is cardboard.
				QPainterPath body;
				body.moveTo(s * 0.10, s * 0.80);
				body.lineTo(s * 0.10, s * 0.24);
				body.lineTo(s * 0.40, s * 0.24);
				body.lineTo(s * 0.48, s * 0.34);
				body.lineTo(s * 0.90, s * 0.34);
				body.lineTo(s * 0.90, s * 0.80);
				body.closeSubpath();
				QLinearGradient manila(0, s * 0.24, 0, s * 0.80);
				manila.setColorAt(0.0, QColor(0xE8, 0xC8, 0x7E));
				manila.setColorAt(1.0, QColor(0xC7, 0xA1, 0x52));
				painter.setPen(QPen(QColor(0x8F, 0x6F, 0x2E), outlineWidth));
				painter.setBrush(manila);
				painter.drawPath(body);
				// Catch-light along the tab edge.
				painter.setPen(QPen(QColor(0xFF, 0xEC, 0xBC), qMax(1.0, s * 0.04)));
				painter.drawLine(QPointF(s * 0.13, s * 0.27), QPointF(s * 0.38, s * 0.27));
				break;
			}
			case Glyph::ConfigFile:
				paperSheet([&]() {
					painter.setPen(QPen(QColor(0x9A, 0x94, 0x82), qMax(1.0, s * 0.05)));
					painter.drawLine(QPointF(s * 0.32, s * 0.46), QPointF(s * 0.68, s * 0.46));
					painter.drawLine(QPointF(s * 0.32, s * 0.58), QPointF(s * 0.68, s * 0.58));
					painter.drawLine(QPointF(s * 0.32, s * 0.70), QPointF(s * 0.56, s * 0.70));
				});
				break;
			case Glyph::AudioFile:
				paperSheet([&]() {
					// The label strip a tape reel would carry: an amber trace.
					painter.setPen(QPen(amber, qMax(1.0, s * 0.06), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
					QPainterPath wave;
					wave.moveTo(s * 0.31, s * 0.60);
					wave.lineTo(s * 0.41, s * 0.46);
					wave.lineTo(s * 0.53, s * 0.70);
					wave.lineTo(s * 0.66, s * 0.52);
					painter.drawPath(wave);
				});
				break;
			case Glyph::PluginFile:
				paperSheet([&]() {
					// A DIP chip on the sheet: the outboard unit's brain.
					painter.setPen(QPen(QColor(0x2A, 0x2E, 0x33), qMax(1.0, s * 0.04)));
					painter.setBrush(QColor(0x3A, 0x40, 0x47));
					painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.20));
					painter.drawLine(QPointF(s * 0.43, s * 0.48), QPointF(s * 0.43, s * 0.41));
					painter.drawLine(QPointF(s * 0.50, s * 0.48), QPointF(s * 0.50, s * 0.41));
					painter.drawLine(QPointF(s * 0.57, s * 0.48), QPointF(s * 0.57, s * 0.41));
				});
				break;
			case Glyph::GenericFile:
				paperSheet([]() {});
				break;
			case Glyph::Drive:
			{
				// A rack drive slab: brushed dark metal, milled slot, power LED.
				QLinearGradient metal(0, s * 0.30, 0, s * 0.72);
				metal.setColorAt(0.0, QColor(0x3C, 0x44, 0x4C));
				metal.setColorAt(1.0, QColor(0x20, 0x26, 0x2B));
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(metal);
				painter.drawRoundedRect(QRectF(s * 0.10, s * 0.30, s * 0.80, s * 0.42), s * 0.04, s * 0.04);
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), qMax(1.0, s * 0.04)));
				painter.drawLine(QPointF(s * 0.18, s * 0.60), QPointF(s * 0.56, s * 0.60));
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(0x7C, 0xE8, 0xA8));
				painter.drawEllipse(QPointF(s * 0.78, s * 0.58), s * 0.045, s * 0.045);
				break;
			}
			case Glyph::Computer:
			{
				// The studio's CRT monitor: dark bezel, powered screen.
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(QColor(0x2C, 0x32, 0x38));
				painter.drawRoundedRect(QRectF(s * 0.12, s * 0.16, s * 0.76, s * 0.50), s * 0.05, s * 0.05);
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(0x14, 0x1A, 0x14));
				painter.drawRect(QRectF(s * 0.20, s * 0.24, s * 0.60, s * 0.34));
				painter.setBrush(QColor(0x7C, 0xE8, 0xA8));
				painter.drawRect(QRectF(s * 0.24, s * 0.30, s * 0.22, s * 0.045));
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(QColor(0x2C, 0x32, 0x38));
				painter.drawRect(QRectF(s * 0.44, s * 0.66, s * 0.12, s * 0.10));
				painter.drawRect(QRectF(s * 0.30, s * 0.76, s * 0.40, s * 0.06));
				break;
			}
			}
		});
	}
};
// ── SPECTRUM MONITOR: the analysis dock's response graph ───────────────────
// The same oscilloscope law as the GraphicEQ scope (RackChrome), adapted to
// a wide always-on monitoring unit. The engraving and lamp idioms below are
// file-local reproductions of RackChrome's grammar, the way
// RackReferenceCardView restates them, so RackChrome itself stays frame-only.

// Engraved faceplate printing: a contrast pass offset one pixel down (the
// recess edge catching the worklight), then the body ink on top.
void monitorEngrave(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

// A panel LED in a bezel ring: lit = glowing dome with a specular dot,
// unlit = the same dome gone dark.
void monitorLamp(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(70, 62, 50, 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (lit)
	{
		QRadialGradient halo(center, radius * 3.2);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, radius * 3.2, radius * 3.2);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	if (lit)
	{
		dome.setColorAt(0.0, litColor.lighter(150));
		dome.setColorAt(1.0, litColor.darker(125));
	}
	else
	{
		const QColor off = litColor.darker(330);
		dome.setColorAt(0.0, off.lighter(140));
		dome.setColorAt(1.0, off);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(dome);
	painter.drawEllipse(center, radius, radius);
	painter.setBrush(QColor(255, 255, 255, lit ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

// The instrument itself: a dark phosphor-glass window in BOTH finishes (the
// display law) over a faceplate strip carrying the engraved unit printing;
// the region above 0 dB is the danger-red OVER zone while the response can
// clip. Nothing here is a widget or a timer - a stateless painter.
void paintAnalysisMonitor(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	const double hover = qBound(0.0, state.hover, 1.0);
	painter.save();
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// Display-glass idiom shared with the GEQ scope and the LCD wells: the
	// glass is dark in both finishes, the graticule lives in the scope-grid
	// family (the cream table's grid token is panel paint, so it never
	// reaches the glass), the phosphor is the machine's LED green lifted to
	// emission strength on the cream finish, and the OVER voice is the
	// danger red lifted the same way.
	const QColor glassTop = dark ? QColor(0x04, 0x06, 0x05) : QColor(0x0A, 0x0E, 0x0B);
	const QColor glassBottom = dark ? QColor(0x0A, 0x0F, 0x0C) : QColor(0x11, 0x16, 0x10);
	const QColor bezelInk = dark ? QColor(0x05, 0x08, 0x07) : QColor(0x4A, 0x44, 0x38);
	const QColor bezelLip = dark ? QColor(0x39, 0x42, 0x4A) : QColor(0x6B, 0x63, 0x54);
	const QColor gridMinor = dark ? QColor(tokens.graphGridMinor) : QColor(0x25, 0x43, 0x37);
	const QColor gridMajor = gridMinor.lighter(168);
	const QColor phosphor = dark ? QColor(tokens.accent2) : QColor(tokens.accent2).lighter(195);
	const QColor segmentBright = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
	const QColor segmentDim = dark ? QColor(0x4C, 0x9E, 0x74) : QColor(0x2F, 0x8A, 0x61);
	// The OVER voice is the danger red of a hardware PEAK lamp, not the
	// panel's amber accent: overdrive is damage. Only barely lifted on the
	// cream finish - the glass is dark in BOTH finishes, so a strong lift
	// only washes the red toward pink.
	const QColor overInk = dark ? QColor(tokens.danger) : QColor(tokens.danger).lighter(115);

	const QRectF full(state.rect);
	const qreal plateHeight = 16.0;
	const QRectF plate(full.left(), full.bottom() - plateHeight, full.width(), plateHeight);
	const QRectF glassFrame = QRectF(full).adjusted(0.5, 0.5, -0.5, -plateHeight - 0.5);

	// ── The faceplate strip under the glass ──
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor plateColor(tokens.card);
	QLinearGradient plateSheen(plate.topLeft(), plate.bottomLeft());
	plateSheen.setColorAt(0.0, plateColor.lighter(dark ? 114 : 103));
	plateSheen.setColorAt(1.0, plateColor.darker(dark ? 110 : 105));
	painter.fillRect(plate, plateSheen);
	// The machined bottom edge: the dark rack seam under the plate.
	painter.setPen(QPen(dark ? QColor(0x06, 0x08, 0x09) : QColor(0x8F, 0x82, 0x68), 1));
	painter.drawLine(state.rect.left(), state.rect.bottom(), state.rect.right(), state.rect.bottom());

	// Plate printing: engraved designation left, the footer caption centre,
	// the OVER lamp with its legend right. The designation and OVER are
	// hardware printing (never translated); the caption is localized data
	// engraved as-is - no uppercasing, no tracking.
	const QRectF plateText = plate.adjusted(10.0, 1.0, -10.0, -2.0);
	QFont plateFont(tokens.fontFamily);
	plateFont.setPixelSize(8);
	plateFont.setBold(true);
	plateFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
	const QFontMetricsF plateMetrics(plateFont);

	const qreal lampRadius = 3.0;
	const QPointF lampCenter(plateText.right() - lampRadius, plate.center().y());
	qreal reservedRight = lampRadius * 2.0 + 6.0;
	if (plateText.width() >= 220.0)
	{
		painter.setFont(plateFont);
		const QString overLegend = QStringLiteral("OVER");
		const QRectF legendRect(plateText.left(), plateText.top(),
			plateText.width() - reservedRight, plateText.height());
		monitorEngrave(painter, legendRect, Qt::AlignRight | Qt::AlignVCenter, overLegend,
			state.clipping ? withAlpha(overInk, 245) : withAlpha(QColor(tokens.mutedText), dark ? 140 : 180), dark);
		reservedRight += plateMetrics.horizontalAdvance(overLegend) + 8.0;
	}

	qreal reservedLeft = 0.0;
	const QString designation = QStringLiteral("SPECTRUM MONITOR");
	const qreal designationWidth = plateMetrics.horizontalAdvance(designation);
	if (plateText.width() >= designationWidth * 2.6)
	{
		painter.setFont(plateFont);
		monitorEngrave(painter, plateText, Qt::AlignLeft | Qt::AlignVCenter, designation,
			withAlpha(QColor(tokens.mutedText), dark ? 150 : 190), dark);
		reservedLeft = designationWidth + 14.0;
	}

	if (!state.channelText.isEmpty())
	{
		QFont captionFont(tokens.fontFamily);
		captionFont.setPixelSize(9);
		const QFontMetricsF captionMetrics(captionFont);
		const QRectF captionRect(plateText.left() + reservedLeft, plateText.top(),
			qMax(0.0, plateText.width() - reservedLeft - reservedRight), plateText.height());
		if (captionRect.width() >= 40.0)
		{
			painter.setFont(captionFont);
			monitorEngrave(painter, captionRect, Qt::AlignCenter,
				captionMetrics.elidedText(state.channelText, Qt::ElideRight, captionRect.width()),
				withAlpha(QColor(tokens.text), dark ? 175 : 205), dark);
		}
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	monitorLamp(painter, lampCenter, lampRadius, overInk, state.clipping, dark);

	// ── The glass window ──
	QPainterPath glassPath;
	glassPath.addRoundedRect(glassFrame, 2.0, 2.0);
	painter.save();
	painter.setClipPath(glassPath);

	QLinearGradient ground(glassFrame.topLeft(), glassFrame.bottomLeft());
	ground.setColorAt(0.0, glassTop);
	ground.setColorAt(1.0, glassBottom);
	painter.fillRect(glassFrame, ground);

	// The beam's memory warms the tube; entry hover pre-heats the phosphor.
	QRadialGradient backGlow(state.plotRect.center(), qMax(1.0, state.plotRect.width() * 0.55));
	backGlow.setColorAt(0.0, withAlpha(phosphor, qRound(10.0 + 8.0 * hover)));
	backGlow.setColorAt(1.0, withAlpha(phosphor, 0));
	painter.setPen(Qt::NoPen);
	painter.setBrush(backGlow);
	painter.drawRect(state.plotRect);

	// The OVER zone: while the response can clip, the band above the 0 dB
	// axis glows danger-red under the graticule - hot at the top of the
	// glass, dying at the axis, the way an overdriven tube warns.
	const qreal zeroY = state.zeroY;
	const bool overZone = state.clipping && zeroY > state.plotRect.top() + 1.0;
	if (overZone)
	{
		QLinearGradient warn(QPointF(0.0, state.plotRect.top()), QPointF(0.0, zeroY));
		warn.setColorAt(0.0, withAlpha(overInk, 56));
		warn.setColorAt(1.0, withAlpha(overInk, 10));
		painter.fillRect(QRectF(state.plotRect.left(), state.plotRect.top(),
			state.plotRect.width(), zeroY - state.plotRect.top()), warn);
	}

	// Graticule: crisp 1px rules - straight lines carry no antialiasing (the
	// scope law shared with the GEQ display). Inside the OVER zone the rules
	// turn to the danger-red warning graticule.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());
	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	const int zeroRow = int(zeroY);
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		const int x = int(line.pos);
		if (overZone)
		{
			painter.setPen(QPen(withAlpha(overInk, line.major ? 120 : 78), 1));
			painter.drawLine(x, plotTop, x, zeroRow - 1);
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(x, zeroRow, x, plotBottom);
		}
		else
		{
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(x, plotTop, x, plotBottom);
		}
	}
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		const int y = int(line.pos);
		if (overZone && y < zeroRow)
			painter.setPen(QPen(withAlpha(overInk, line.major ? 120 : 78), 1));
		else
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(plotLeft, y, plotRight, y);
	}

	// The zero axis: a phosphor-tinted centre line with the scope's fine
	// hash ticks - the boundary the OVER zone burns against. Struck only while
	// the metric's zero sits inside the fitted range; a group delay that never
	// goes negative would otherwise get an axis printed along the glass edge.
	if (state.zeroVisible)
	{
		painter.setPen(QPen(withAlpha(phosphor, 145), 1));
		painter.drawLine(plotLeft, zeroRow, plotRight, zeroRow);
		painter.setPen(QPen(withAlpha(phosphor, 60), 1));
		for (int x = plotLeft + 4; x < plotRight - 2; x += 7)
			painter.drawLine(x, zeroRow - 2, x, zeroRow + 2);
	}

	// Axis figures: etched in segment ink (numerals - hardware printing,
	// never translated). The dB column reads inside the left graticule edge
	// and takes the warning ink above the axis while the zone is hot; dense
	// fits thin the column to 12/24 dB steps so figures never collide.
	QFont axisFont(tokens.monoFontFamily);
	axisFont.setPointSizeF(7.0);
	axisFont.setBold(true);
	painter.setFont(axisFont);
	qreal dbGap = 1000.0;
	for (int i = 1; i < state.horizontal.size(); i++)
		dbGap = qMin(dbGap, qAbs(state.horizontal.at(i).pos - state.horizontal.at(i - 1).pos));
	const int labelStep = dbGap >= 13.0 ? 6 : (dbGap >= 6.5 ? 12 : 24);
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		if (line.label.isEmpty() || line.label.toInt() % labelStep != 0)
			continue;
		const bool overFigure = overZone && line.pos < zeroY - 1.0;
		painter.setPen(withAlpha(overFigure ? overInk : segmentDim, line.major ? 235 : 150));
		painter.drawText(QRect(plotLeft + 4, int(line.pos) - 8, 34, 16),
			Qt::AlignLeft | Qt::AlignVCenter, line.label);
	}
	const int glassBottomRow = int(glassFrame.bottom());
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(segmentDim, line.major ? 235 : 150));
		painter.drawText(QRect(int(line.pos) - 24, plotBottom, 48, glassBottomRow - plotBottom),
			Qt::AlignHCenter | Qt::AlignVCenter, line.label);
	}

	// ── The beam ── (masked to the graticule area, like a tube's trace)
	painter.save();
	painter.setClipRect(state.plotRect.adjusted(-1, -1, 1, 1), Qt::IntersectClip);
	painter.setRenderHint(QPainter::Antialiasing, true);
	// One sweep per segment. The metric goes dark where it has no value, and the
	// beam blanks with it: a tube that flew across the gap would burn in a
	// reading the machine never received.
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;

		const double base = qBound(state.plotRect.top(), zeroY, state.plotRect.bottom());

		// Afterglow: the faint phosphor wash between the trace and the axis.
		QPolygonF afterglow = segment;
		afterglow.append(QPointF(segment.last().x(), base));
		afterglow.prepend(QPointF(segment.first().x(), base));
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(phosphor, qRound(18.0 + 8.0 * hover)));
		painter.drawPolygon(afterglow);

		// The trace: glow faked by stroke overpainting (no effects on this
		// machine); the entry hover intensifies the phosphor.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(phosphor, qRound(20.0 + 12.0 * hover)), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);
		painter.setPen(QPen(withAlpha(phosphor, qRound(58.0 + 22.0 * hover)), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);
		painter.setPen(QPen(phosphor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);

		// Above the axis the beam burns danger-red: the same passes redrawn
		// inside the OVER band only, hotter than the phosphor ever gets, plus
		// a white-hot core - an overdriven beam, not an annotation.
		if (overZone)
		{
			painter.save();
			painter.setClipRect(QRectF(state.plotRect.left() - 1.0, state.plotRect.top() - 1.0,
				state.plotRect.width() + 2.0, zeroY - state.plotRect.top() + 1.0), Qt::IntersectClip);
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(overInk, qRound(38.0 + 10.0 * hover)));
			painter.drawPolygon(afterglow);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(overInk, qRound(44.0 + 14.0 * hover)), 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(withAlpha(overInk, qRound(105.0 + 26.0 * hover)), 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(overInk, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(withAlpha(mixColor(overInk, QColor(255, 255, 255), 0.55), 215), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.restore();
		}
	}
	painter.restore();

	// ── The measurement cursor ── a scope cursor line with grab ticks, a
	// brightened measured point on the beam and a segment readout in the
	// glass corner.
	if (state.cursorValid)
	{
		const int cursorColumn = int(state.cursor.x());
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(withAlpha(segmentBright, 110), 1));
		painter.drawLine(cursorColumn, plotTop, cursorColumn, plotBottom);
		painter.setPen(QPen(withAlpha(segmentBright, 220), 1));
		painter.drawLine(cursorColumn, plotTop, cursorColumn, plotTop + 5);
		painter.drawLine(cursorColumn, plotBottom - 5, cursorColumn, plotBottom);

		painter.setRenderHint(QPainter::Antialiasing, true);
		const bool overPoint = state.curveYAtCursor < zeroY - 0.5;
		const QColor mark = overPoint ? overInk : phosphor;
		const QPointF measured(state.cursor.x(), state.curveYAtCursor);
		QRadialGradient halo(measured, 8.0);
		halo.setColorAt(0.0, withAlpha(mark, 90));
		halo.setColorAt(1.0, withAlpha(mark, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(measured, 8.0, 8.0);
		painter.setBrush(mark.lighter(130));
		painter.drawEllipse(measured, 2.6, 2.6);

		if (!state.cursorText.isEmpty())
		{
			QFont readoutFont(tokens.monoFontFamily);
			readoutFont.setPointSizeF(7.5);
			readoutFont.setBold(true);
			painter.setFont(readoutFont);
			const QFontMetricsF readoutMetrics(readoutFont);
			painter.setPen(segmentBright);
			painter.drawText(QRectF(state.plotRect.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop,
				readoutMetrics.elidedText(state.cursorText, Qt::ElideLeft, qMax(0.0, state.plotRect.width() - 16.0)));
		}
	}

	// The bezel's overhang shadow hangs over the top of the glass - the
	// recessed grammar (shadowed top lip, lit lower lip below).
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient overhang(glassFrame.topLeft(), QPointF(glassFrame.left(), glassFrame.top() + 9.0));
	overhang.setColorAt(0.0, QColor(0, 0, 0, dark ? 150 : 130));
	overhang.setColorAt(1.0, QColor(0, 0, 0, 0));
	painter.fillRect(QRectF(glassFrame.left(), glassFrame.top(), glassFrame.width(), 9.0), overhang);

	painter.restore();

	// Bezel frame and the lit lower lip between glass and plate.
	painter.setBrush(Qt::NoBrush);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(bezelInk, 1));
	painter.drawRoundedRect(glassFrame, 2.0, 2.0);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(bezelLip, 1));
	painter.drawLine(int(glassFrame.left()) + 2, int(glassFrame.bottom()), int(glassFrame.right()) - 2, int(glassFrame.bottom()));

	painter.restore();
}

class RackSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("rack"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static HardwarePatchbayRoutingRenderer renderer;
		return &renderer;
	}

	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		RackChrome::paintKnob(painter, rect, state, tokens);
	}

	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		// state.label is a UI string, not hardware printing, so the stencil
		// ignores it - the widget's tooltip keeps the translated caption
		// reachable.
		RackChrome::paintAddRow(painter, rect, state, tokens);
	}

	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		RackChrome::paintInsertSeam(painter, rect, state, tokens);
	}

	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		RackChrome::paintGraphicEqPlot(painter, state, tokens);
	}

	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override
	{
		// The SPECTRUM MONITOR unit (paintAnalysisMonitor above).
		paintAnalysisMonitor(painter, state, tokens);
	}

	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// QSS only provides the machined base plate and the hover brightening;
		// the faceplate texture, ears, screws and LEDs are painted on top by
		// RackChrome::paintCardChrome (the sheen overlays are translucent, so
		// the hover state shines through them). The resting border is the dark
		// seam of the rack opening rather than the token border, so stacked
		// units separate physically; focus and selection keep their signal
		// colours.
		const bool dark = skinIsDark(tokens);
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

		// Unparsed lines (bare text, programmatic commands like If) are the
		// AUX unit's programming LCD: the as-written line burns in green
		// segments in a dark recessed well, in both finishes - displays never
		// follow the panel finish. The row widget seeds this label with an
		// inline token style QSS cannot beat, so the display law is applied
		// here, and a powered-down unit dims its segments at the same time
		// (rows are rebuilt whenever the line's state changes).
		if ((info.type == QStringLiteral("text") || info.type == QStringLiteral("if")
			|| info.type == QStringLiteral("eval") || info.dynamicLine) && body != nullptr)
		{
			if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				const SkinTokens tk = SkinManager::instance()->tokens();
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

	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		RackChrome::paintCardChrome(painter, rect, info, tokens);
	}

	// The If-block scope is a relay-switched power bus in the gutter (drawn
	// in RackChrome). Branch/tail rows mount at member depth so the lane
	// passes them instead of dying behind their faceplates.
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		return RackChrome::paintScopeGutter(painter, size, info, tokens);
	}

	bool logicSiblingsIndentAsMembers() const override
	{
		return true;
	}

	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		// QSS prints the model designation and dresses the caption buttons
		// as machined caps; RackChrome paints the panel around them.
		RackChrome::paintTitleBarChrome(painter, rect, tokens);
	}

	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new RackFilterPickerView(parent);
	}

	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new RackReferenceCardView(kind, parent);
	}

	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		// The shared stroke icons first, then the master-rail overlay
		// RackChrome mounts under the toolbar's controls; the QSS dresses
		// the controls themselves.
		ISkin::styleMainToolbar(toolBar, tokens);
		RackChrome::styleMainToolbar(toolBar, tokens);
	}

	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override
	{
		// The shared stroke icons on the navigation row, then the transport
		// treatment: each nav button is tagged so the sheet raises it into a
		// machined cap like the main toolbar's keys (round-2 verdict: "위쪽
		// 툴바도 버튼처럼"). The attribute selector outranks the shared
		// fileDialogOverride padding reset, so the caps keep their own fit.
		ISkin::styleFileDialog(dialog, tokens);
		if (dialog == nullptr)
			return;
		const char* const navButtons[] = {
			"backButton", "forwardButton", "toParentButton",
			"newFolderButton", "listModeButton", "detailModeButton"
		};
		for (const char* name : navButtons)
		{
			QToolButton* button = dialog->findChild<QToolButton*>(QLatin1String(name));
			if (button == nullptr)
				continue;
			button->setProperty("rackTransport", true);
			button->style()->unpolish(button);
			button->style()->polish(button);
		}
		// The shelf objects behind the machine: skeuomorphic folder/file
		// pictograms.
		static RackFileIconProvider iconProvider;
		iconProvider.updateTokens(tokens);
		dialog->setIconProvider(&iconProvider);
	}

	// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).
};
}

ISkin* rackSkin()
{
	static RackSkin instance;
	return &instance;
}
