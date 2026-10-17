/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "HardwarePatchbayRoutingRenderer.h"
#include "Editor/skins/rack/RackPalette.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QPainter>
#include <QFontMetrics>

using std::vector;

HardwarePatchbayView::HardwarePatchbayView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: GridRoutingViewBase(assignments, channelNames, portModel, parent, tokens,
		// Widened to the engraved labels in updateMetrics(); the defaults fit
		// a Copy row's short device names.
		{ .rowHeaderWidth = 60, .colHeaderHeight = 34, .cellW = 56, .cellH = 50 },
		QStringLiteral("PatchbayEditor"), QStringLiteral("PatchbayChannelEditor"))
{
	rebuildMatrix();
}

void HardwarePatchbayView::updateMetrics()
{
	// The stock faceplate widths fit the short device names of a Copy row
	// (L, R, SL, ...). The subwoofer crossover dialog mounts channels like
	// FrontBass and SourceLFE on the same panel, and an engraved label
	// longer than its column was clipped to fragments ("ntBass"). The
	// engraving font decides the width a column really needs.
	QFont label(skinTokens.monoFontFamily);
	label.setPixelSize(12);
	label.setLetterSpacing(QFont::AbsoluteSpacing, 1);
	const QFontMetrics fm(label);

	int longestInput = 0;
	for (const QString& ch : matrix.inputs)
		longestInput = qMax(longestInput, fm.horizontalAdvance(ch));
	int longestOutput = 0;
	for (const QString& out : matrix.outputs)
		longestOutput = qMax(longestOutput, fm.horizontalAdvance(out));

	cellW = qMax(56, longestInput + 12);
	// The row label right-aligns into rowHeaderWidth - 11 (the unpatch
	// target's slot stays clear).
	rowHeaderWidth = qMax(60, longestOutput + 17);
}

QRect HardwarePatchbayView::stripRect() const
{
	return QRect(0, colHeaderHeight + matrix.outputs.size() * cellH + 10, width(), stripH);
}

QSize HardwarePatchbayView::sizeHint() const
{
	int w = rowHeaderWidth + matrix.inputs.size() * cellW + 8;
	int h = colHeaderHeight + matrix.outputs.size() * cellH + 8;
	h += 10 + stripH + 4;
	w = qMax(w, 240);
	return QSize(w, h);
}

void HardwarePatchbayView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	const bool dark = skinTokens.dark;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	// Brushed metal panel background.
	QLinearGradient panel(0, 0, 0, height());
	panel.setColorAt(0, QColor(t.surface).lighter(108));
	panel.setColorAt(1, QColor(t.surface).darker(108));
	p.fillRect(rect(), panel);

	// The button field: a recessed sub-panel the switch bank is mounted in
	// (shadowed top edge, lit lower lip - the sheet's recessed grammar, the
	// capture well's orientation).
	if (!matrix.outputs.isEmpty() && !matrix.inputs.isEmpty())
	{
		const QRect field(rowHeaderWidth - 5, colHeaderHeight - 5,
			matrix.inputs.size() * cellW + 10, matrix.outputs.size() * cellH + 10);
		p.setPen(Qt::NoPen);
		p.setBrush(dark ? QColor(t.surface).darker(122) : QColor(t.surface).darker(106));
		p.drawRoundedRect(field, 3, 3);
		p.setPen(QPen(RackPalette::PatchFieldShadowEdge(dark), 1));
		p.drawLine(field.left() + 2, field.top(), field.right() - 2, field.top());
		p.setPen(QPen(RackPalette::PatchFieldLitEdge(dark), 1));
		p.drawLine(field.left() + 2, field.bottom(), field.right() - 2, field.bottom());
	}

	// Engraved header labels: the faceplate's tracked lettering; channel
	// colour stays the cross-skin data ink.
	QFont label(t.monoFontFamily);
	label.setPixelSize(12);
	label.setLetterSpacing(QFont::AbsoluteSpacing, 1);
	p.setFont(label);

	// Column (input) engraved labels.
	for (int c = 0; c < matrix.inputs.size(); ++c)
	{
		const QString ch = matrix.inputs[c];
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		p.setPen(col);
		p.drawText(QRect(rowHeaderWidth + c * cellW, 0, cellW, colHeaderHeight - 5), Qt::AlignCenter, ch);
	}

	// The button legend is printed type on the cap, not tracked engraving.
	QFont legend(t.monoFontFamily);
	legend.setPixelSize(11);
	legend.setBold(true);

	// The control-cap recipe, shared by the crosspoint blanks and the
	// faceplate strip buttons: raised = lit top bevel, latched = inverted
	// bevel with the face dropped 1px (the machine's one latch law).
	auto drawCap = [&](QRect cap, bool latched, const QString& capText, bool prelit) {
		if (latched)
			cap.translate(0, 1);
		QLinearGradient face(cap.topLeft(), cap.bottomLeft());
		if (latched)
		{
			face.setColorAt(0, RackPalette::CapFaceLatchedTop(dark));
			face.setColorAt(1, RackPalette::CapFaceLatchedBottom(dark));
		}
		else
		{
			face.setColorAt(0, (prelit ? RackPalette::CapFacePrelitTop : RackPalette::CapFaceTop)(dark));
			face.setColorAt(1, RackPalette::CapFaceBottom(dark));
		}
		p.setBrush(face);
		p.setPen(QPen(RackPalette::CapOutline(dark), 1));
		p.drawRoundedRect(cap, 2, 2);
		if (latched)
		{
			p.setPen(QPen(RackPalette::PatchFieldShadowEdge(dark), 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
			p.setPen(QPen(RackPalette::PatchFieldLitEdge(dark), 1));
			p.drawLine(cap.left() + 2, cap.bottom() - 1, cap.right() - 2, cap.bottom() - 1);
		}
		else
		{
			p.setPen(QPen(RackPalette::PatchFieldLitEdge(dark), 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
		}
		if (!capText.isEmpty())
		{
			QColor capInk = RackPalette::CapLegend(dark);
			if (prelit || latched)
				capInk = RackPalette::CapLegendLit(dark);
			p.setFont(legend);
			p.setPen(capInk);
			p.drawText(cap, Qt::AlignCenter, capText);
		}
	};

	removeRects.clear();
	removeRects.resize(matrix.outputs.size());

	for (int r = 0; r < matrix.outputs.size(); ++r)
	{
		const QString out = matrix.outputs[r];
		const QColor col(CopyRoutingAdapter::channelColor(out));
		p.setFont(label);
		p.setPen(col);
		const QRect labelRect(0, colHeaderHeight + r * cellH, rowHeaderWidth - 11, cellH);
		p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignRight, out);

		// A virtual channel label can be unpatched from the faceplate:
		// hovering its row engraves an x target under the label (device
		// channels fold instead of leaving, so they never get one).
		if (portModel.isVirtualChannel(out) && hoveredRow == r)
		{
			const QRect xr(labelRect.right() - 12, labelRect.center().y() + 8, 14, 14);
			p.setPen(withAlpha(col, 230));
			p.drawText(xr, Qt::AlignCenter, QStringLiteral("x"));
			removeRects[r] = xr;
		}

		for (int c = 0; c < matrix.inputs.size(); ++c)
		{
			const QRect cr = cellRect(r, c);
			const CopyRoutingAdapter::Cell cell = matrix.cell(r, c);

			// The crosspoint button cap. Colour values mirror the Device
			// switch bank in rack_dark.qss / rack_light.qss - one machine,
			// one latch law.
			const int capW = qMin(cellW - 10, 46);
			const int capH = 30;
			QRect cap(cr.center().x() - capW / 2, cr.center().y() - capH / 2, capW, capH);

			if (!cell.present)
			{
				// At rest: a raised blank cap (lit top bevel), with a small
				// engraved actuator dimple so the empty position still reads
				// as a press target.
				QLinearGradient face(cap.topLeft(), cap.bottomLeft());
				face.setColorAt(0, RackPalette::BlankCapFaceTop(dark));
				face.setColorAt(1, RackPalette::CapFaceBottom(dark));
				p.setBrush(face);
				p.setPen(QPen(RackPalette::CapOutline(dark), 1));
				p.drawRoundedRect(cap, 2, 2);
				p.setPen(QPen(RackPalette::PatchFieldLitEdge(dark), 1));
				p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
				p.setBrush(RackPalette::CapDimple(dark));
				p.setPen(Qt::NoPen);
				p.drawEllipse(cap.center() + QPoint(1, 1), 2, 2);
				continue;
			}

			// Routed: the cap sits latched down (shadowed top edge, lit lower
			// lip, face dropped 1px) with the lamp lit under it - amber for a
			// routing, the danger lamp for a polarity/negative gain.
			const bool negative = cell.factor < 0;
			cap.translate(0, 1);
			QLinearGradient face(cap.topLeft(), cap.bottomLeft());
			QColor edge, bevelTop, bevelBottom, ink;
			if (negative)
			{
				face.setColorAt(0, RackPalette::NegativeCapFaceTop(dark));
				face.setColorAt(1, RackPalette::NegativeCapFaceBottom(dark));
				edge = QColor(t.danger);
				bevelTop = RackPalette::NegativeCapBevelTop(dark);
				bevelBottom = RackPalette::NegativeCapBevelBottom(dark);
				ink = RackPalette::NegativeCapLegend(dark);
			}
			else
			{
				face.setColorAt(0, RackPalette::RoutedCapFaceTop(dark));
				face.setColorAt(1, RackPalette::RoutedCapFaceBottom(dark));
				edge = QColor(t.accent);
				bevelTop = RackPalette::RoutedCapBevelTop(dark);
				bevelBottom = RackPalette::RoutedCapBevelBottom(dark);
				ink = RackPalette::RoutedCapLegend(dark);
			}
			p.setBrush(face);
			p.setPen(QPen(edge, 1));
			p.drawRoundedRect(cap, 2, 2);
			p.setPen(QPen(bevelTop, 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
			p.setPen(QPen(bevelBottom, 1));
			p.drawLine(cap.left() + 2, cap.bottom() - 1, cap.right() - 2, cap.bottom() - 1);

			// A unity routing carries no legend: the lit latch already says
			// "passed through as-is", and any printed value on it (x1, 0dB)
			// invites a volume/mute misreading. The legend appears only when
			// the coefficient deviates - INV for a plain polarity flip, the
			// bare coefficient otherwise, dB kept as authored.
			const bool unity = cell.factor == 1.0 && !cell.isDecibel;
			if (portModel.allowFactors && !unity)
			{
				QString capText;
				if (cell.factor == -1.0 && !cell.isDecibel)
					capText = QStringLiteral("INV");
				else
					capText = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor);
				p.setFont(legend);
				p.setPen(ink);
				p.drawText(cap, Qt::AlignCenter, capText);
			}
			else
			{
				// Unity Copy point, or a factor-less MultiConvolution patch
				// point: a blank cap with a ROUND lamp window glowing in it -
				// the console ON-button LED. A filled dot with a halo: a
				// horizontal slit would read as a minus sign and an outline
				// ring as the letter O.
				const QPointF lampCenter(cap.center().x() + 0.5, cap.center().y() + 0.5);
				p.setPen(Qt::NoPen);
				p.setBrush(withAlpha(ink, 70));
				p.drawEllipse(lampCenter, 5.5, 5.5);
				p.setBrush(withAlpha(ink, 235));
				p.drawEllipse(lampCenter, 3.0, 3.0);
			}
		}
	}

	// Faceplate strip: the expansion latch and the ADD button, engraved and
	// capped like every other control on the machine. The latch stays down
	// while the full device layout is mounted.
	revealRect = QRect();
	addRect = QRect();
	const QRect strip = stripRect();
	QFontMetrics fm(label);
	int x = 6;

	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString capText = channelsExpanded
			? QStringLiteral("ALL")
			: QStringLiteral("+%1").arg(fold.hiddenChannels);
		revealRect = QRect(x, strip.top(), 44, 30);
		drawCap(revealRect, channelsExpanded, capText, hoveredControl == 1);
		// Tracked engraving under the cap's right shoulder names the bank.
		p.setFont(label);
		p.setPen(withAlpha(QColor(t.mutedText), 220));
		const QString engrave = QStringLiteral("CHANNELS");
		p.drawText(QRect(x + 52, strip.top(), fm.horizontalAdvance(engrave) + 8, 30),
			Qt::AlignVCenter | Qt::AlignLeft, engrave);
		x += 52 + fm.horizontalAdvance(engrave) + 24;
	}

	addRect = QRect(x, strip.top(), 44, 30);
	drawCap(addRect, false, QStringLiteral("ADD"), hoveredControl == 2);
}

QRect HardwarePatchbayView::factorEditorRect(int outRow, int inCol) const
{
	const QRect cr = cellRect(outRow, inCol);
	return QRect(cr.left() + 6, cr.center().y() - 10, cr.width() - 12, 20);
}

QRect HardwarePatchbayView::channelEditorRect() const
{
	return addRect.isNull()
		? QRect(6, stripRect().top() + 4, 110, 24)
		: QRect(addRect.right() + 10, addRect.top() + 3, 110, 24);
}

RoutingView* HardwarePatchbayRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new HardwarePatchbayView(assignments, channelNames, portModel, parent, tokens);
}
