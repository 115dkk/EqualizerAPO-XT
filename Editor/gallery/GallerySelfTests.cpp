/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The card self tests: --selftest-vst (store/parse round trip and the
	channel-fill menus), --routing-edit-test, --scroll-bench and
	--power-toggle-test.
*/

#include "SkinGallery.h"
#include "Editor/gallery/GallerySupport.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// For the VST round-trip self test, which moved here from main.cpp (audit
// #275 B7).
#include <optional>
#include <unordered_map>
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QSet>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/guis/VSTPluginFilterGUI.h"
#include "Editor/skins/HeritageSkin.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/skins/rack/cards/RackReferenceCardView.h"
#include "Editor/widgets/ButtonName.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/cards/VSTPluginSession.h"
#include "Editor/widgets/cards/VSTSlotFillBlock.h"
#include "Editor/widgets/cards/VSTSlotFillRail.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingSectionHead.h"
#include "vst/VST3BusLayout.h"

using namespace SkinGalleryDetail;

namespace
{
// A paint device that draws nothing and records where a painter put its
// text and its small shapes, so a check can compare what a skin painter
// actually drew against the cell it was given (audit #348 F10).
class FillCellRecorder : public QPaintDevice
{
public:
	struct Text
	{
		QString text;
		qreal left = 0.0;
		qreal right = 0.0;
		qreal top = 0.0;
		qreal bottom = 0.0;
	};

	explicit FillCellRecorder(const QSize& size) : engine(this), deviceSize(size) {}

	QPaintEngine* paintEngine() const override { return &engine; }

	QList<Text> texts;
	QList<QRectF> shapes;

protected:
	int metric(PaintDeviceMetric metric) const override
	{
		switch (metric)
		{
		case PdmWidth: return deviceSize.width();
		case PdmHeight: return deviceSize.height();
		case PdmWidthMM: return qRound(deviceSize.width() * 25.4 / 96.0);
		case PdmHeightMM: return qRound(deviceSize.height() * 25.4 / 96.0);
		case PdmNumColors: return INT_MAX;
		case PdmDepth: return 32;
		case PdmDpiX:
		case PdmDpiY:
		case PdmPhysicalDpiX:
		case PdmPhysicalDpiY: return 96;
		default: return QPaintDevice::metric(metric);
		}
	}

private:
	class Engine : public QPaintEngine
	{
	public:
		explicit Engine(FillCellRecorder* owner) : QPaintEngine(QPaintEngine::AllFeatures), owner(owner) {}
		bool begin(QPaintDevice*) override { return true; }
		bool end() override { return true; }
		Type type() const override { return QPaintEngine::User; }
		void updateState(const QPaintEngineState& state) override
		{
			if (state.state() & QPaintEngine::DirtyTransform)
				transform = state.transform();
		}
		void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
		void drawTextItem(const QPointF& p, const QTextItem& item) override
		{
			const QRectF box = transform.mapRect(QRectF(p.x(), p.y() - item.ascent(), item.width(),
				item.ascent() + item.descent()));
			owner->texts.append({ item.text(), box.left(), box.right(), box.top(), box.bottom() });
		}
		void drawPath(const QPainterPath& path) override
		{
			owner->shapes.append(transform.map(path).boundingRect());
		}
		void drawPolygon(const QPointF* points, int pointCount, PolygonDrawMode) override
		{
			owner->shapes.append(transform.map(QPolygonF(QList<QPointF>(points, points + pointCount))).boundingRect());
		}

	private:
		FillCellRecorder* owner = nullptr;
		QTransform transform;
	};

	mutable Engine engine;
	QSize deviceSize;
};

// A small shape a painter adds next to a channel (the caret), as the
// recorder saw it: anything this small that is filled.
bool isCaretShape(const QRectF& shape)
{
	return shape.width() <= 8.0 && shape.height() <= 8.0 && shape.width() > 0.0;
}

// The channel-fill slot lists the block check lays out: equal lists, two
// different lists, and each side alone. Values include the "-" slot.
QList<VSTSlotFillBlock::CellData> fillRow(const QStringList& roles, const QStringList& values)
{
	QList<VSTSlotFillBlock::CellData> row;
	for (int i = 0; i < roles.size(); i++)
	{
		VSTSlotFillBlock::CellData cell;
		cell.role = roles[i];
		cell.value = values[i];
		cell.silent = values[i] == QLatin1String("-");
		row.append(cell);
	}
	return row;
}

// A joined fill block (VstSlotFillArrangement::JoinedBlockBelowBody) must
// hold what its skin paints, in the real geometry VSTSlotFillBlock uses
// (computeLayout over the skin's metrics and column widths):
// - every value cell, role, row caption and the latch inside the block's
//   band (or, folded, inside the block's size), none overlapping another;
// - a role over the same column as its cell (both rows' cells on a shared
//   role line), a shared role line between the two rows, a per-row role
//   line next to its own row;
// - a LeftColumn latch exactly as tall as a value cell and centred on the
//   rows;
// - every text the block painter draws (captions, roles, latch legend or
//   heading) inside the rect it was given, and every channel the cell
//   painter draws inside its cell and clear of its caret.
// Returns an empty string or the first problem.
QString checkFillBlock(const ISkin* skin, const SkinTokens& tokens)
{
	const QStringList five = { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
		QStringLiteral("LFE"), QStringLiteral("RL"), QStringLiteral("RR") };
	const QStringList seven = { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
		QStringLiteral("LFE"), QStringLiteral("RL"), QStringLiteral("RR"), QStringLiteral("SL"), QStringLiteral("SR") };
	const QList<VSTSlotFillBlock::CellData> in51 = fillRow(five,
		{ QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("-"), QStringLiteral("SL"), QStringLiteral("SR") });
	const QList<VSTSlotFillBlock::CellData> out51 = fillRow(five, five);
	const QList<VSTSlotFillBlock::CellData> out71 = fillRow(seven,
		{ QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("-"),
			QStringLiteral("RL"), QStringLiteral("RR"), QStringLiteral("SBL"), QStringLiteral("SBR") });
	struct Case
	{
		const char* name = "";
		QList<VSTSlotFillBlock::CellData> input;
		QList<VSTSlotFillBlock::CellData> output;
		bool collapsed = false;
	};
	const QList<Case> cases = {
		{ "5.1/5.1", in51, out51, false },
		{ "5.1/7.1", in51, out71, false },
		{ "in only", in51, {}, false },
		{ "out only", {}, out71, false },
		{ "folded", in51, out51, true },
	};
	const VSTSlotFillBlock::ColumnWidth columnWidth = [skin, &tokens](const QString& role, const QString& value, bool output) {
			return skin->vstSlotFillBlockColumnWidth(role, value, output, tokens);
		};

	for (const Case& c : cases)
	{
		const QString label = QString::fromLatin1(c.name);
		const bool latchPresent = !c.input.isEmpty() && !c.output.isEmpty();
		const VstSlotFillBlockMetrics metrics = skin->vstSlotFillBlockMetrics(c.collapsed, tokens);
		const VSTSlotFillBlock::Layout layout = VSTSlotFillBlock::computeLayout(metrics, columnWidth,
			c.input, c.output, latchPresent, c.collapsed);
		const bool folded = c.collapsed && latchPresent;
		const bool leftLatch = metrics.latchPlacement == VstSlotFillBlockMetrics::LatchPlacement::LeftColumn;
		const QRect bounds = folded ? QRect(QPoint(0, 0), layout.size) : layout.bandRect;
		if (bounds.isEmpty() || layout.size != bounds.size())
			return QStringLiteral("%1: the block has no size").arg(label);

		// Everything the block places, for the containment and overlap tests.
		QList<QPair<QString, QRect>> parts;
		if (!layout.latchRect.isNull())
			parts.append({ QStringLiteral("latch"), layout.latchRect });
		for (int i = 0; i < layout.inputCells.size(); i++)
			parts.append({ QStringLiteral("IN cell %1").arg(i), layout.inputCells[i] });
		for (int i = 0; i < layout.outputCells.size(); i++)
			parts.append({ QStringLiteral("OUT cell %1").arg(i), layout.outputCells[i] });
		for (const VstSlotFillBlockState::Caption& caption : layout.captions)
			parts.append({ caption.output ? QStringLiteral("OUT caption") : QStringLiteral("IN caption"), caption.rect });
		for (const VstSlotFillBlockState::Role& role : layout.roles)
			parts.append({ QStringLiteral("%1 role %2").arg(role.output ? QStringLiteral("OUT") : QStringLiteral("IN"), role.role),
				role.rect });
		for (int i = 0; i < parts.size(); i++)
		{
			if (!bounds.contains(parts[i].second))
				return QStringLiteral("%1: %2 sticks out of the block").arg(label, parts[i].first);
			for (int j = i + 1; j < parts.size(); j++)
			{
				if (parts[i].second.intersects(parts[j].second))
					return QStringLiteral("%1: %2 overlaps %3").arg(label, parts[i].first, parts[j].first);
			}
		}
		if (folded)
		{
			if (!layout.inputCells.isEmpty() || !layout.outputCells.isEmpty() || layout.latchRect.isNull())
				return QStringLiteral("%1: a folded block must be the latch alone").arg(label);
		}
		else
		{
			if (layout.inputCells.size() != c.input.size() || layout.outputCells.size() != c.output.size())
				return QStringLiteral("%1: cell count").arg(label);
			// Roles over their columns.
			for (const VstSlotFillBlockState::Role& role : layout.roles)
			{
				const QList<QRect>& cells = role.output ? layout.outputCells : layout.inputCells;
				const QList<VSTSlotFillBlock::CellData>& data = role.output ? c.output : c.input;
				int slot = -1;
				for (int i = 0; i < cells.size(); i++)
				{
					if (cells[i].left() == role.rect.left() && cells[i].width() == role.rect.width())
						slot = i;
				}
				if (slot < 0 || data[slot].role != role.role)
					return QStringLiteral("%1: role %2 is not over its column").arg(label, role.role);
				const QRect& cell = cells[slot];
				const int gap = role.rect.top() >= cell.bottom()
					? role.rect.top() - cell.bottom() - 1 : cell.top() - role.rect.bottom() - 1;
				const QList<QRect>& otherCells = role.output ? layout.inputCells : layout.outputCells;
				const bool sharedLine = !layout.inputCells.isEmpty() && !layout.outputCells.isEmpty()
					&& layout.roles.size() == c.input.size();
				if (sharedLine)
				{
					// Between the rows, and over the other row's cell too.
					if (!(role.rect.top() > layout.inputCells[0].bottom() && role.rect.bottom() < layout.outputCells[0].top()))
						return QStringLiteral("%1: the shared role line is not between the rows").arg(label);
					if (otherCells[slot].left() != role.rect.left() || otherCells[slot].width() != role.rect.width())
						return QStringLiteral("%1: the rows' columns do not align at %2").arg(label, role.role);
				}
				else if (gap < 0 || gap > metrics.roleGap)
					return QStringLiteral("%1: role %2 is %3 px from its row").arg(label, role.role).arg(gap);
			}
			if (leftLatch && !layout.latchRect.isNull())
			{
				if (layout.latchRect.height() != metrics.cellHeight)
					return QStringLiteral("%1: latch is %2 px tall, a cell %3").arg(label)
						.arg(layout.latchRect.height()).arg(metrics.cellHeight);
				const int top = (layout.inputCells.isEmpty() ? layout.outputCells : layout.inputCells).first().top();
				int bottom = 0;
				for (const QRect& cell : layout.outputCells.isEmpty() ? layout.inputCells : layout.outputCells)
					bottom = qMax(bottom, cell.bottom());
				for (const VstSlotFillBlockState::Role& role : layout.roles)
					bottom = qMax(bottom, role.rect.bottom());
				const int centre = layout.latchRect.top() + layout.latchRect.bottom();
				int rowsTop = top;
				for (const VstSlotFillBlockState::Role& role : layout.roles)
					rowsTop = qMin(rowsTop, role.rect.top());
				if (qAbs(centre - (rowsTop + bottom)) > 1)
					return QStringLiteral("%1: the latch is not centred on the rows").arg(label);
			}
		}

		// What the block painter draws stays in the rects it was given.
		VstSlotFillBlockState state;
		state.rect = QRect(QPoint(0, 0), layout.size);
		state.bandRect = folded ? QRect() : layout.bandRect;
		state.latchRect = layout.latchRect;
		state.latchActive = layout.latchActive;
		state.collapsed = folded;
		if (!folded)
		{
			state.captions = layout.captions;
			state.roles = layout.roles;
		}
		FillCellRecorder blockRecorder(layout.size);
		{
			QPainter painter(&blockRecorder);
			skin->paintVstSlotFillBlock(painter, state, tokens);
		}
		QList<QRect> textHomes;
		for (const VstSlotFillBlockState::Caption& caption : state.captions)
			textHomes.append(caption.rect);
		for (const VstSlotFillBlockState::Role& role : state.roles)
			textHomes.append(role.rect);
		if (!state.latchRect.isNull())
			textHomes.append(state.latchRect);
		const qreal epsilon = 0.01;
		for (const FillCellRecorder::Text& text : blockRecorder.texts)
		{
			const qreal middle = (text.top + text.bottom) / 2.0;
			bool home = false;
			for (const QRect& rect : textHomes)
			{
				if (text.left >= rect.left() - epsilon && text.right <= rect.right() + 1 + epsilon
					&& middle >= rect.top() && middle <= rect.bottom() + 1)
					home = true;
			}
			if (!home)
				return QStringLiteral("%1: \"%2\" (%3..%4) is outside every rect the block gave").arg(label, text.text)
					.arg(text.left, 0, 'f', 1).arg(text.right, 0, 'f', 1);
		}
		for (const VstSlotFillBlockState::Role& role : state.roles)
		{
			bool printed = false;
			for (const FillCellRecorder::Text& text : blockRecorder.texts)
			{
				if (text.text.compare(role.role, Qt::CaseInsensitive) == 0
					&& text.left >= role.rect.left() - epsilon && text.right <= role.rect.right() + 1 + epsilon)
					printed = true;
			}
			if (!printed)
				return QStringLiteral("%1: role %2 is not printed in its column").arg(label, role.role);
		}

		// Every channel inside its cell and clear of the caret.
		for (int side = 0; side < 2; side++)
		{
			const bool output = side == 1;
			const QList<QRect>& cells = output ? layout.outputCells : layout.inputCells;
			const QList<VSTSlotFillBlock::CellData>& data = output ? c.output : c.input;
			for (int i = 0; i < cells.size(); i++)
			{
				FillCellRecorder recorder(layout.size);
				VstSlotFillCellState cellState;
				cellState.rect = QRect(QPoint(0, 0), cells[i].size());
				cellState.output = output;
				cellState.roleToken = data[i].role;
				cellState.valueText = data[i].value;
				cellState.silent = data[i].silent;
				cellState.roleInBlock = true;
				{
					QPainter painter(&recorder);
					skin->paintVstSlotFillCell(painter, cellState, tokens);
				}
				if (recorder.texts.isEmpty())
					return QStringLiteral("%1: cell %2 printed nothing").arg(label, data[i].role);
				const FillCellRecorder::Text value = recorder.texts.last();
				if (value.left < -epsilon || value.right > cells[i].width() + epsilon)
					return QStringLiteral("%1: channel %2 spans %3..%4 in a %5 px cell").arg(label, data[i].value)
						.arg(value.left, 0, 'f', 1).arg(value.right, 0, 'f', 1).arg(cells[i].width());
				for (const QRectF& shape : recorder.shapes)
				{
					if (isCaretShape(shape) && shape.right() > value.left && shape.left() < value.right - epsilon)
						return QStringLiteral("%1: channel %2 overlaps the caret").arg(label, data[i].value);
				}
			}
		}
	}
	return QString();
}

// Every skin's fill must hold the text its own painter draws. A skin on the
// split rails is checked cell by cell: the cell, sized by that skin's
// vstSlotFillCellSize, holds the role and the channel, the role before the
// channel, and the channel clear of the caret (any small filled shape the
// painter adds). A skin on the joined block is checked through
// checkFillBlock above, over every role/channel pair as a single column
// too. Returns the number of failing skins.
//
// Heritage is checked with the five: its cell painter is the former ISkin
// default (audit #422 S3), measured by the ISkin default sizer, so the pair
// stays honest although heritage never builds the VST card.
int checkFillCellFit()
{
	QStringList roles;
	for (const VST3BusLayoutDefinition& definition : vst3ExplicitBusLayouts())
	{
		for (const wchar_t* name : definition.channelNames)
		{
			if (!roles.contains(QString::fromWCharArray(name)))
				roles.append(QString::fromWCharArray(name));
		}
	}
	QStringList values = roles;
	for (const QString& extra : { QStringLiteral("-"), QStringLiteral("SBL"), QStringLiteral("SBR"),
			QStringLiteral("FLC"), QStringLiteral("FRC"), QStringLiteral("VSL"), QStringLiteral("12") })
		values.append(extra);

	QList<ISkin*> skins = Skins::all();
	skins.append(heritageSkin());
	int failures = 0;
	for (ISkin* skin : skins)
	{
		const SkinTokens tokens = skin->tokens(true);
		QString problem;
		int widest = 0;
		QString widestLabel;
		if (skin->vstSlotFillArrangement() == VstSlotFillArrangement::JoinedBlockBelowBody)
		{
			problem = checkFillBlock(skin, tokens);
			// Every role/channel pair as one column: the role on its line
			// and the channel in its cell, both inside the column width.
			const VstSlotFillBlockMetrics metrics = skin->vstSlotFillBlockMetrics(false, tokens);
			for (int i = 0; problem.isEmpty() && i < roles.size(); i++)
			{
				for (int j = 0; problem.isEmpty() && j < values.size(); j++)
				{
					for (int side = 0; problem.isEmpty() && side < 2; side++)
					{
						const bool output = side == 1;
						const QList<VSTSlotFillBlock::CellData> row = fillRow({ roles[i] }, { values[j] });
						const VSTSlotFillBlock::Layout layout = VSTSlotFillBlock::computeLayout(metrics,
							[skin, &tokens](const QString& role, const QString& value, bool isOutput) {
								return skin->vstSlotFillBlockColumnWidth(role, value, isOutput, tokens);
							},
							output ? QList<VSTSlotFillBlock::CellData>() : row,
							output ? row : QList<VSTSlotFillBlock::CellData>(), false, false);
						const QRect cell = output ? layout.outputCells.value(0) : layout.inputCells.value(0);
						const QString label = roles[i] + QLatin1Char('/') + values[j];
						if (cell.width() > widest)
						{
							widest = cell.width();
							widestLabel = label;
						}
						VstSlotFillBlockState state;
						state.rect = QRect(QPoint(0, 0), layout.size);
						state.roles = layout.roles;
						FillCellRecorder roleRecorder(layout.size);
						{
							QPainter painter(&roleRecorder);
							skin->paintVstSlotFillBlock(painter, state, tokens);
						}
						for (const FillCellRecorder::Text& text : roleRecorder.texts)
						{
							if (text.left < cell.left() - 0.01 || text.right > cell.right() + 1.01)
								problem = QStringLiteral("%1: role spans %2..%3 over a column %4..%5").arg(label)
									.arg(text.left, 0, 'f', 1).arg(text.right, 0, 'f', 1).arg(cell.left()).arg(cell.right() + 1);
						}
						FillCellRecorder recorder(cell.size());
						VstSlotFillCellState cellState;
						cellState.rect = QRect(QPoint(0, 0), cell.size());
						cellState.output = output;
						cellState.roleToken = roles[i];
						cellState.valueText = values[j];
						cellState.silent = values[j] == QLatin1String("-");
						cellState.roleInBlock = true;
						{
							QPainter painter(&recorder);
							skin->paintVstSlotFillCell(painter, cellState, tokens);
						}
						if (recorder.texts.isEmpty())
							problem = QStringLiteral("%1: the cell printed nothing").arg(label);
						else if (recorder.texts.last().left < -0.01 || recorder.texts.last().right > cell.width() + 0.01)
							problem = QStringLiteral("%1: channel spans %2..%3 in a %4 px cell").arg(label)
								.arg(recorder.texts.last().left, 0, 'f', 1).arg(recorder.texts.last().right, 0, 'f', 1)
								.arg(cell.width());
					}
				}
			}
			if (problem.isEmpty())
			{
				fprintf(stderr, "[VST fill selftest] fit %s: OK (joined block: 5 layouts, %d role/channel columns, widest column %d px for %s)\n",
					skin->id().toUtf8().constData(), int(roles.size() * values.size() * 2), widest,
					widestLabel.toUtf8().constData());
			}
			else
			{
				failures++;
				fprintf(stderr, "[VST fill selftest] fit %s: FAIL, %s\n", skin->id().toUtf8().constData(),
					problem.toUtf8().constData());
			}
			continue;
		}
		for (const QString& role : roles)
		{
			for (const QString& value : values)
			{
				const QSize size = skin->vstSlotFillCellSize(role, value, tokens);
				FillCellRecorder recorder(size);
				VstSlotFillCellState state;
				state.rect = QRect(QPoint(0, 0), size);
				state.roleToken = role;
				state.valueText = value;
				{
					QPainter painter(&recorder);
					skin->paintVstSlotFillCell(painter, state, tokens);
				}
				const QString label = role + QLatin1Char('/') + value;
				if (size.width() > widest)
				{
					widest = size.width();
					widestLabel = label;
				}
				// Painters draw the role (engraved skins twice) and then the
				// channel, so the last text item is the channel.
				if (recorder.texts.size() < 2 || recorder.texts.last().text != value)
				{
					problem = QStringLiteral("%1: expected the role then the channel as text").arg(label);
					break;
				}
				const FillCellRecorder::Text valueText = recorder.texts.last();
				qreal roleRight = 0.0;
				qreal left = valueText.left;
				for (int i = 0; i < recorder.texts.size() - 1; i++)
				{
					roleRight = qMax(roleRight, recorder.texts[i].right);
					left = qMin(left, recorder.texts[i].left);
				}
				const qreal epsilon = 0.01;
				if (left < -epsilon || valueText.right > size.width() + epsilon)
					problem = QStringLiteral("%1: text spans %2..%3 in a %4 px cell").arg(label)
						.arg(left, 0, 'f', 1).arg(valueText.right, 0, 'f', 1).arg(size.width());
				else if (roleRight > valueText.left + epsilon)
					problem = QStringLiteral("%1: role ends at %2, channel starts at %3").arg(label)
						.arg(roleRight, 0, 'f', 1).arg(valueText.left, 0, 'f', 1);
				for (const QRectF& shape : recorder.shapes)
				{
					const bool caret = isCaretShape(shape);
					if (caret && shape.right() > valueText.left && shape.left() < valueText.right - epsilon)
						problem = QStringLiteral("%1: channel %2..%3 overlaps the caret at %4..%5").arg(label)
							.arg(valueText.left, 0, 'f', 1).arg(valueText.right, 0, 'f', 1)
							.arg(shape.left(), 0, 'f', 1).arg(shape.right(), 0, 'f', 1);
				}
				if (!problem.isEmpty())
					break;
			}
			if (!problem.isEmpty())
				break;
		}
		if (problem.isEmpty())
		{
			fprintf(stderr, "[VST fill selftest] fit %s: OK (%d role/channel pairs, widest cell %d px for %s)\n",
				skin->id().toUtf8().constData(), int(roles.size() * values.size()), widest,
				widestLabel.toUtf8().constData());
		}
		else
		{
			failures++;
			fprintf(stderr, "[VST fill selftest] fit %s: FAIL, %s\n", skin->id().toUtf8().constData(),
				problem.toUtf8().constData());
		}
	}
	return failures;
}
}

// A reference card's label strip takes the width its labels' size hints ask
// for, so a label given exactly that width is on a plate with room and must
// print its whole text; only a label squeezed below its hint may elide. The
// hint and the paint-time elision measured the text two ways (a rounded
// integer advance against the fractional one), and every name whose advance
// rounded down printed elided on an empty plate: example.txt on the rack
// Include card, at 13 px and at 14 px alike. Each text is rendered into
// the recorder once at the hint (whole) and once 8 px narrower (elided, so
// the check stays live). Covers both elidable faces the card uses: the
// reference name and the location prefix under it.
int SkinGalleryDetail::checkButtonNames(QWidget* scene, const QString& capture)
{
	static QSet<QString> reported;
	int failures = 0;
	for (QAbstractButton* button : scene->findChildren<QAbstractButton*>())
	{
		if (!button->isVisibleTo(scene) || button->toolTip().isEmpty())
			continue;
		// A channel chip is named by its channel, however short ("L", "C").
		const QString objectName = button->objectName();
		if (objectName == QLatin1String("ChannelChip") || objectName == QLatin1String("NewChannelChip"))
			continue;
		// Asked the way the platform's accessibility bridge asks.
		QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(button);
		const QString spoken = accessible != nullptr ? accessible->text(QAccessible::Name) : QString();
		if (buttonLabelHasWords(spoken))
			continue;
		const QString kind = QStringLiteral("%1#%2|%3").arg(
			QString::fromLatin1(button->metaObject()->className()), objectName, button->toolTip());
		if (reported.contains(kind))
			continue;
		reported.insert(kind);
		qWarning("SkinGallery: %s: a button with the tooltip '%s' is named '%s' for a screen reader (%s#%s)",
			qPrintable(capture), qPrintable(button->toolTip()), qPrintable(spoken),
			button->metaObject()->className(), qPrintable(objectName));
		failures++;
	}
	return failures;
}

int SkinGalleryDetail::checkEngravedLabelFit()
{
	ISkin* rack = nullptr;
	for (ISkin* skin : Skins::all())
	{
		if (skin->id() == QLatin1String("rack"))
			rack = skin;
	}
	if (rack == nullptr)
	{
		fprintf(stderr, "[label fit selftest] FAIL, no rack skin\n");
		return 1;
	}

	QStringList texts = {
		QStringLiteral("example.txt"), QStringLiteral("example.wav"), QStringLiteral("TestVst3Plugin"),
		QStringLiteral("Upmixer"), QStringLiteral("Surround\\"),
		QStringLiteral("E:\\eapo-wt\\rack-elide\\Tests\\TestVst3Plugin\\x64\\Release\\")};
	// Every prefix of two longer names, so the advances land on many
	// different fractions of a pixel.
	for (const QString& sweep : { QStringLiteral("Hall_Large_48k_Stereo_Impulse.wav"),
			QStringLiteral("C:\\Program Files\\EqualizerAPO\\config\\") })
	{
		for (int length = 3; length <= sweep.size(); length++)
			texts.append(sweep.left(length));
	}

	struct Face { const char* name = nullptr; int pixelSize = 0; qreal letterSpacing = 0.0; bool bold = true; };
	// The values RackReferenceCardView gives nameLabel and dirLabel.
	const Face faces[] = { { "name", 14, 0.4, true }, { "location", 11, 0.0, false } };

	int failures = 0;
	int checked = 0;
	for (const bool dark : { true, false })
	{
		const SkinTokens tokens = rack->tokens(dark);
		for (const Face& face : faces)
		{
			for (const QString& text : texts)
			{
				RackEngravedLabel label(tokens);
				label.setPixelSize(face.pixelSize);
				label.setLetterSpacing(face.letterSpacing);
				label.setBoldFace(face.bold);
				label.setElideMode(Qt::ElideMiddle);
				label.setText(text);

				const QSize hint = label.sizeHint();
				const auto printedWhole = [&](int width) {
					label.resize(width, hint.height());
					FillCellRecorder recorder(label.size());
					label.render(&recorder, QPoint(), QRegion(), QWidget::RenderFlags());
					if (recorder.texts.isEmpty())
						return false;
					for (const FillCellRecorder::Text& item : recorder.texts)
					{
						if (item.text != text)
							return false;
					}
					return true;
				};

				checked++;
				const char* mode = dark ? "dark" : "light";
				if (!printedWhole(hint.width()))
				{
					failures++;
					fprintf(stderr, "[label fit selftest] %s %s '%s': elided at its own hint width %d px\n",
						mode, face.name, text.toUtf8().constData(), hint.width());
				}
				else if (text.size() > 6 && printedWhole(hint.width() - 8))
				{
					failures++;
					fprintf(stderr, "[label fit selftest] %s %s '%s': printed whole in %d px, below its hint %d px\n",
						mode, face.name, text.toUtf8().constData(), hint.width() - 8, hint.width());
				}
			}
		}
	}
	fprintf(stderr, "[label fit selftest] %s (%d texts, %d failure(s))\n", failures == 0 ? "PASS" : "FAIL",
		checked, failures);
	return failures;
}

// A tooltip label matches only the sheets' QToolTip selectors, never their
// QWidget rule, so while the QToolTip rules named no font every tooltip kept
// the system message face (Malgun Gothic 9pt on a Korean Windows) beside the
// skin's type; the Device Selector, on Qt's default font engine, even drew
// it bold. In every skin and mode the label must be set like the body text
// a bare widget gets: one of the skin's faces first, at the body's size and
// weight.
int SkinGalleryDetail::checkTooltipFaces(const QStringList& skinIds)
{
	int failures = 0;
	for (const QString& listed : skinIds)
	{
		const QString skinId = listed.trimmed();
		for (const bool dark : { true, false })
		{
			SkinManager::instance()->applySkin(skinId, dark);
			const SkinTokens& tokens = SkinManager::instance()->tokens();
			const char* mode = dark ? "dark" : "light";

			QWidget anchor;
			anchor.setToolTip(QStringLiteral("Tooltip text"));
			anchor.resize(240, 40);
			anchor.show();
			anchor.ensurePolished();
			QApplication::processEvents();
			QToolTip::showText(anchor.mapToGlobal(anchor.rect().center()), anchor.toolTip(), &anchor);
			QWidget* tip = nullptr;
			QElapsedTimer waited;
			waited.start();
			while (tip == nullptr && waited.elapsed() < 2000)
			{
				QApplication::processEvents(QEventLoop::AllEvents, 50);
				for (QWidget* widget : QApplication::topLevelWidgets())
				{
					if (widget->inherits("QTipLabel"))
						tip = widget;
				}
			}

			if (tip == nullptr)
			{
				failures++;
				fprintf(stderr, "[tooltip face selftest] %s %s: the tooltip did not open\n", qPrintable(skinId), mode);
			}
			else
			{
				const QFont tipFont = tip->font();
				const QFont body = anchor.font();
				const QString face = tipFont.families().value(0);
				if (face != tokens.fontFamily && face != tokens.monoFontFamily)
				{
					failures++;
					fprintf(stderr, "[tooltip face selftest] %s %s: set in \"%s\", not in the skin's face\n",
						qPrintable(skinId), mode, qPrintable(face));
				}
				if (!qFuzzyCompare(tipFont.pointSizeF(), body.pointSizeF()) || tipFont.weight() != body.weight())
				{
					failures++;
					fprintf(stderr, "[tooltip face selftest] %s %s: %.1fpt weight %d, the body text is %.1fpt weight %d\n",
						qPrintable(skinId), mode, tipFont.pointSizeF(), int(tipFont.weight()), body.pointSizeF(),
						int(body.weight()));
				}
			}
			QToolTip::hideText();
			QApplication::processEvents();
		}
	}
	fprintf(stderr, "[tooltip face selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures;
}

// Mechanical round-trip check for VST plugin data: parse a VSTPlugin line, feed
// the parsed library, opaque state and options into the real VSTPluginFilterGUI,
// call its store(), reparse the result and confirm ChunkData, parameters,
// StereoInput and the hidden Input/Output contract survive. Returns 0 on success,
// 1 on any loss.
int SkinGallery::runVstRoundTripSelfTest()
{
	struct Case { const wchar_t* name = nullptr; std::wstring params; };
	const Case cases[] = {
		{ L"chunkData", L"Library \"fake plugin.dll\" ChunkData \"QUJDREVGR0g=\"" },
		{ L"paramMap", L"Library fake.dll Gain 0.5 Mix 0.25 Width 1" },
		{ L"paramMap-quoted-name", L"Library fake.dll \"Dry/Wet\" 0.75 Output 0.5" },
		{ L"stereoInput-chunk", L"Library fake.dll StereoInput 1 ChunkData \"QUJDREVGR0g=\"" },
		{ L"stereoInput-params", L"Library fake.dll StereoInput 1 Gain 0.5" },
		{ L"busContract", L"Library fake.vst3 Input Stereo Output 7.1 Gain 0.5" },
		{ L"busContract-slotFill",
		  L"Library fake.vst3 Input 5.1 InputChannels L,R,C,-,SL,SR Output 5.1 OutputChannels L,R,C,LFE,RL,RR Gain 0.5" }
	};

	int failures = 0;
	for (const Case& c : cases)
	{
		VSTPluginFilterFactory factory(VSTPluginFilterFactory::Mode::ParseOnly);
		std::wstring command = L"VSTPlugin";
		std::wstring params = c.params;
		FilterVector filters = factory.createFilter(L"", command, params).filters;
		if (filters.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: parse produced no filter\n", c.name);
			failures++;
			continue;
		}
		VSTPluginFilter* f0 = static_cast<VSTPluginFilter*>(filters[0].get());
		std::wstring chunk0 = f0->getChunkData();
		std::unordered_map<std::wstring, float> map0 = f0->getParamMap();
		const bool stereo0 = f0->getStereoInput();
		const std::optional<VST3BusContract> bus0 = f0->getBusContract();
		const std::vector<std::wstring> fillIn0 = f0->getInputChannels();
		const std::vector<std::wstring> fillOut0 = f0->getOutputChannels();

		VSTPluginFilterGUI gui(f0->getLibrary(), chunk0, map0, stereo0, bus0, fillIn0, fillOut0);
		QString outCommand, outParams;
		gui.store(outCommand, outParams);

		std::wstring command2 = outCommand.toStdWString();
		std::wstring params2 = outParams.toStdWString();
		FilterVector filters2 = factory.createFilter(L"", command2, params2).filters;
		if (filters2.empty())
		{
			fprintf(stderr, "[VST selftest] %ls: re-parse produced no filter (params='%ls')\n", c.name, params2.c_str());
			failures++;
			continue;
		}
		VSTPluginFilter* f1 = static_cast<VSTPluginFilter*>(filters2[0].get());
		std::wstring chunk1 = f1->getChunkData();
		std::unordered_map<std::wstring, float> map1 = f1->getParamMap();
		const bool stereo1 = f1->getStereoInput();
		const std::optional<VST3BusContract> bus1 = f1->getBusContract();
		const bool sameBusContract = bus0.has_value() == bus1.has_value()
			&& (!bus0 || (bus0->input == bus1->input && bus0->output == bus1->output));
		const bool sameSlotFill = fillIn0 == f1->getInputChannels() && fillOut0 == f1->getOutputChannels();
		bool ok = (chunk0 == chunk1) && (map0 == map1) && (stereo0 == stereo1) && sameBusContract && sameSlotFill;
		if (!ok)
		{
			failures++;
			fprintf(stderr, "[VST selftest] %ls: LOSS. chunk %ls->%ls, params %zu->%zu, stereoInput %d->%d, bus %d->%d, fill %zu/%zu->%zu/%zu\n",
				c.name, chunk0.c_str(), chunk1.c_str(), map0.size(), map1.size(), stereo0 ? 1 : 0,
				stereo1 ? 1 : 0, bus0 ? 1 : 0, bus1 ? 1 : 0,
				fillIn0.size(), fillOut0.size(), f1->getInputChannels().size(), f1->getOutputChannels().size());
			for (auto& kv : map0)
			{
				auto it = map1.find(kv.first);
				if (it == map1.end())
					fprintf(stderr, "    dropped param '%ls'=%g\n", kv.first.c_str(), kv.second);
				else if (it->second != kv.second)
					fprintf(stderr, "    param '%ls' %g -> %g\n", kv.first.c_str(), kv.second, it->second);
			}
		}
		else
		{
			fprintf(stderr, "[VST selftest] %ls: OK (chunk len %zu, %zu params preserved)\n",
				c.name, chunk0.size(), map0.size());
		}
	}

	fprintf(stderr, "[VST selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runVstFillSelfTest()
{
	// A 7.1 endpoint (the layout Windows offers for the field setup); the
	// row itself negotiates Stereo in / 5.1 out like a stereo upmixer. Each
	// case lists the rows above the VST row and the channels its fill menus
	// must offer: the device set, a Channel row's narrowed selection, and
	// the device set again when that Channel row is powered off (the engine
	// skips commented lines).
	struct Case { const char* name = nullptr; QList<QString> rowsAbove; QStringList expected; };
	const QStringList deviceChannels = {
		QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"),
		QStringLiteral("RL"), QStringLiteral("RR"), QStringLiteral("SL"), QStringLiteral("SR")};
	const Case cases[] = {
		{ "device", {}, deviceChannels },
		{ "channel-narrowed", { QStringLiteral("Channel: L R") },
		  { QStringLiteral("L"), QStringLiteral("R") } },
		{ "channel-commented", { QStringLiteral("# Channel: L R") }, deviceChannels }
	};
	const QString vstLine = QStringLiteral("VSTPlugin: Library example.vst3 Input Stereo Output 5.1");

	int failures = 0;
	for (const Case& c : cases)
	{
		for (int modeIndex = 0; modeIndex < 2; modeIndex++)
		{
			const bool legacy = modeIndex == 1;
			const QByteArray label = QByteArray(c.name) + (legacy ? "/legacy" : "/cards");
			QScrollArea scrollArea;
			scrollArea.setWidgetResizable(true);
			scrollArea.resize(960, 720);
			FilterTable* table = new FilterTable();
			table->setRenderMode(legacy ? FilterTable::LegacyRows : FilterTable::ModernCards);
			scrollArea.setWidget(table);
			QList<std::shared_ptr<AbstractAPOInfo>> outputDevices, inputDevices;
			galleryDevices(outputDevices, inputDevices);
			std::shared_ptr<AbstractAPOInfo> device =
				std::make_shared<GalleryAPOInfo>(L"Speakers", L"Example Audio", false, true, 8, 0x63F);
			table->updateDeviceAndChannelMask(device, 0x63F);
			table->initialize(&scrollArea, outputDevices, inputDevices);
			QList<QString> lines = c.rowsAbove;
			lines.append(vstLine);
			table->setLines(QString(), lines);
			table->updateGuis();
			// The library named here does not exist, so the loader answers in
			// milliseconds. A wait that still runs to its deadline means the
			// completion did not end it: delivering a posted completion does
			// not make processEvents(WaitForMoreEvents) return, and the wait
			// then sleeps until the deadline timer. That made this self-test
			// take 30 s per case and time its CI step out (PR #469).
			QElapsedTimer loadWait;
			loadWait.start();
			const bool loadsSettled = VSTPluginSession::waitForPendingLoads(30000);
			const qint64 loadWaitMs = loadWait.elapsed();
			if (!loadsSettled || loadWaitMs > 5000)
			{
				fprintf(stderr, "[VST fill selftest] %s: the wait for a load that fails at once took %lld ms (settled=%d)\n",
					label.constData(), static_cast<long long>(loadWaitMs), loadsSettled ? 1 : 0);
				failures++;
			}
			scrollArea.show();
			QApplication::processEvents();

			// Both presentations report the menu of every slot; the cards
			// path through the rail cells, the legacy row through its combos
			// after the fold latch is opened (a defaulted fill starts folded,
			// exactly as the field report found it).
			QList<QStringList> menus;
			if (legacy)
			{
				VSTPluginFilterGUI* gui = table->findChild<VSTPluginFilterGUI*>();
				if (gui == nullptr)
				{
					fprintf(stderr, "[VST fill selftest] %s: no VSTPluginFilterGUI row\n", label.constData());
					failures++;
					continue;
				}
				QCheckBox* latch = gui->findChild<QCheckBox*>();
				if (latch != nullptr && !latch->isChecked())
					latch->setChecked(true);
				QApplication::processEvents();
				for (QComboBox* combo : gui->findChildren<QComboBox*>())
				{
					if (!combo->objectName().isEmpty())
						continue;
					QStringList items;
					for (int i = 0; i < combo->count(); i++)
						items.append(combo->itemData(i).toString());
					menus.append(items);
				}
			}
			else
			{
				for (const VSTSlotFillCell* cell : table->findChildren<VSTSlotFillCell*>())
					menus.append(cell->channelChoices());
			}

			// Stereo in + 5.1 out: two input slots and six output slots.
			if (menus.size() != 8)
			{
				fprintf(stderr, "[VST fill selftest] %s: expected 8 slot menus, found %d\n",
					label.constData(), int(menus.size()));
				failures++;
				continue;
			}
			bool ok = true;
			for (const QStringList& menu : menus)
			{
				QStringList channels = menu;
				channels.removeAll(QStringLiteral("-"));
				if (channels != c.expected)
					ok = false;
			}
			if (!ok)
			{
				failures++;
				fprintf(stderr, "[VST fill selftest] %s: expected %s, menus offer:\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
				for (const QStringList& menu : menus)
					fprintf(stderr, "    %s\n", menu.join(QLatin1Char(',')).toUtf8().constData());
			}
			else
			{
				fprintf(stderr, "[VST fill selftest] %s: OK (%s)\n", label.constData(),
					c.expected.join(QLatin1Char(',')).toUtf8().constData());
			}
		}
	}
	failures += checkFillCellFit();
	fprintf(stderr, "[VST fill selftest] %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}

int SkinGallery::runRoutingEditTest()
{
	qWarning("RoutingEditTest: starting");
	const GalleryFixture fixture("RoutingEditTest");
	if (!fixture.isValid())
		return 2;
	const QString& configPath = fixture.configPath();

	int failures = 0;
	const auto settle = []() {
		QApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
	};
	const auto check = [&failures](bool ok, const QString& what) {
		if (!ok)
		{
			qWarning("RoutingEditTest: %s", qPrintable(what));
			failures++;
		}
		return ok;
	};
	const auto lineText = [](QScrollArea& scrollArea, int row) {
		const FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		return table != nullptr && row < table->documentItems().size()
			? table->documentItems().at(row)->text.trimmed() : QString();
	};
	const auto liveView = [](FilterCardRow* row) -> RoutingView* {
		// MultiConvolution rebuilds its view after file metadata arrives and
		// hides the superseded one until deleteLater lands.
		RoutingView* view = nullptr;
		for (RoutingView* candidate : row->findChildren<RoutingView*>())
			if (candidate->isVisible())
				view = candidate;
		return view;
	};
	const auto pressEnter = [](QWidget* editor) {
		QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(editor, &enterPress);
	};
	// The source editor's own accounting: text plus the sheet's padding and
	// hairline must fit the field, or the token is unreadable.
	const auto editorFits = [](QLineEdit* editor) {
		return editor->fontMetrics().horizontalAdvance(editor->text()) + 14 <= editor->width()
			&& editor->height() >= editor->fontMetrics().height() + 4;
	};
	// Retype the open source editor's token and commit it with Enter; the
	// editor must be the focus widget the view just opened.
	const auto retype = [&](RoutingView* view, const QString& target, const QString& token) -> QLineEdit* {
		view->grab();
		view->galleryShowcase(QStringLiteral("editSource:") + target);
		QApplication::processEvents();
		QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
		if (editor == nullptr || !editor->isVisible())
			return nullptr;
		editor->setText(token);
		pressEnter(editor);
		settle();
		return editor;
	};

	auto stereo = std::make_shared<GalleryAPOInfo>(
		L"Speakers", L"Example Audio", false, true, 2, 0x3);

	// Part 1: the minimal step list over an empty Copy on a stereo endpoint.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy:") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? rows[0]->findChild<RoutingView*>() : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal Copy row has no routing view")))
		{
			failures += 10;
		}
		else
		{
			// Two virtual channels typed at the prompt, the way an upmix is
			// started: they must be offered as sources from then on.
			for (const QString& name : { QStringLiteral("VL"), QStringLiteral("VR") })
			{
				view->galleryShowcase(QStringLiteral("addChannel"));
				QApplication::processEvents();
				QLineEdit* prompt = qobject_cast<QLineEdit*>(QApplication::focusWidget());
				if (!check(prompt != nullptr, QStringLiteral("the add-channel prompt did not take focus")))
					break;
				check(editorFits(prompt) || prompt->text().isEmpty(),
					QStringLiteral("the add-channel prompt clips its text"));
				prompt->setText(name);
				check(editorFits(prompt), QStringLiteral("the add-channel prompt clips '%1'").arg(name));
				pressEnter(prompt);
				settle();
			}
			const QStringList before = view->sourceCandidates(QStringLiteral("L"));
			check(before == QStringList({ QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("first source hint for L is '%1' (expected L R VL VR)").arg(before.join(' ')));

			check(view->connectSource(QStringLiteral("L"), QStringLiteral("L")),
				QStringLiteral("connecting L to L was refused"));
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=L"),
				QStringLiteral("after L=L the line is '%1'").arg(lineText(scrollArea, 0)));
			check(rows[0]->findChild<RoutingView*>() == view,
				QStringLiteral("the Copy row rebuilt its routing view on an edit"));
			// The report: from the second source on, the hint shrank to R.
			const QStringList after = view->sourceCandidates(QStringLiteral("L"));
			check(after == QStringList({ QStringLiteral("R"), QStringLiteral("VL"), QStringLiteral("VR") }),
				QStringLiteral("second source hint for L is '%1' (expected R VL VR)").arg(after.join(' ')));

			// The source editor: readable, and the token grammar of the line.
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:L"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the source editor did not open")))
			{
				check(editor->text() == QStringLiteral("L"),
					QStringLiteral("the source editor holds '%1' (expected L)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("-0.000dB*LFE"));
				check(editorFits(editor),
					QStringLiteral("the source editor (%1 px) clips a full token").arg(editor->width()));
				editor->setText(QStringLiteral("0.5*R"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=0.5*R"),
					QStringLiteral("after retyping 0.5*R the line is '%1'").arg(lineText(scrollArea, 0)));
			}
			if (retype(view, QStringLiteral("L"), QStringLiteral("INV")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=-1.0*R"),
					QStringLiteral("a bare INV keeps the channel: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for INV"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("VL")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("a bare channel is unity: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for VL"));
			if (retype(view, QStringLiteral("L"), QStringLiteral("L+R")) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy: L=VL"),
					QStringLiteral("an unreadable token must leave the line: it is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for L+R"));
			if (retype(view, QStringLiteral("L"), QString()) != nullptr)
				check(lineText(scrollArea, 0) == QStringLiteral("Copy:"),
					QStringLiteral("an emptied token removes the source: line is '%1'").arg(lineText(scrollArea, 0)));
			else
				check(false, QStringLiteral("the source editor did not reopen for removal"));
		}
	}

	// Part 2: MultiConvolution over the 4-channel BRIR - a port retyped in
	// the same editor is the port, not a gain.
	{
		SkinManager::instance()->applySkin(QStringLiteral("minimal"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("MultiConvolution: L=0 R=0 brir.wav") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
		if (!check(view != nullptr, QStringLiteral("minimal MultiConvolution row has no routing view")))
		{
			failures += 4;
		}
		else
		{
			const QStringList hint = view->sourceCandidates(QStringLiteral("R"));
			check(hint == QStringList({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") }),
				QStringLiteral("MultiConvolution source hint for R is '%1' (expected 1 2 3)").arg(hint.join(' ')));
			view->grab();
			view->galleryShowcase(QStringLiteral("editSource:R"));
			QApplication::processEvents();
			QLineEdit* editor = view->findChild<QLineEdit*>(QStringLiteral("StepSourceEditor"));
			if (check(editor != nullptr && editor->isVisible(), QStringLiteral("the MultiConvolution source editor did not open")))
			{
				check(editor->text() == QStringLiteral("0"),
					QStringLiteral("the MultiConvolution source editor holds '%1' (expected 0)").arg(editor->text()));
				check(editorFits(editor),
					QStringLiteral("the MultiConvolution source editor (%1x%2) clips '%3'")
					.arg(editor->width()).arg(editor->height()).arg(editor->text()));
				editor->setText(QStringLiteral("1"));
				pressEnter(editor);
				settle();
				check(lineText(scrollArea, 0) == QStringLiteral("MultiConvolution: L=0 R=1 brir.wav"),
					QStringLiteral("after retyping port 1 the line is '%1'").arg(lineText(scrollArea, 0)));
			}
		}
	}

	// Part 3: the card's body never scrolls on its own. The row hosts its
	// editor in a scroll area whose vertical bar is off; if that area is ever
	// shorter than its content the wheel moves the body inside the card, with
	// nothing on screen to say so. A 16-channel file over a 7.1.4 endpoint,
	// with the target fold opened, is the tallest body a MultiConvolution row
	// has.
	{
		const QDir refs = QFileInfo(configPath).dir();
		check(writeWavFile(refs.filePath(QStringLiteral("brir16.wav")), 16, 48000, 4800),
			QStringLiteral("could not write the 16-channel test file"));
		auto atmos = std::make_shared<GalleryAPOInfo>(
			L"Speakers", L"Example Audio 7.1.4", false, true, 12, 0x2D63F);
		// Copy rides the same body and the same boards, with the endpoint's
		// twelve channels on both sides.
		const QStringList lines = { QStringLiteral("MultiConvolution: L=0 R=1 brir16.wav"), QStringLiteral("Copy: L=R") };
		for (const QString& line : lines)
		for (ISkin* skin : Skins::all())
		{
			const QString command = line.section(QLatin1Char(':'), 0, 0);
			SkinManager::instance()->applySkin(skin->id(), true);
			const QString who = skin->id() + QLatin1Char(' ') + command;
			QScrollArea scrollArea;
			scrollArea.resize(960, 720);
			QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, { line }, atmos, 0x2D63F);
			RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
			if (!check(view != nullptr, QStringLiteral("%1 row has no routing view").arg(who)))
				continue;
			for (const char* state : { "collapsed", "expanded" })
			{
				if (QLatin1String(state) == QLatin1String("expanded"))
					view->galleryShowcase(QStringLiteral("expanded"));
				settle();
				if (FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget()))
					if (table->layout() != nullptr)
						table->layout()->activate();
				settle();
				QScrollArea* body = rows[0]->findChild<QScrollArea*>(QStringLiteral("FilterCardEditorScroll"));
				if (!check(body != nullptr && body->widget() != nullptr,
					QStringLiteral("%1 row has no editor body").arg(who)))
					break;
				const int reach = body->verticalScrollBar()->maximum();
				const QScrollBar* across = body->horizontalScrollBar();
				const int sideways = across->maximum();
				qWarning("RoutingEditTest: body %s %s: scroll %d, content %dx%d (hint %d), reach %d, sideways %d (bar %s, %d px), viewport %dx%d",
					qPrintable(who), state, body->height(), body->widget()->width(), body->widget()->height(),
					body->widget()->sizeHint().height(), reach, sideways, across->isVisible() ? "shown" : "hidden",
					across->height(), body->viewport()->width(), body->viewport()->height());
				check(sideways == 0,
					QStringLiteral("%1 body (%2) is %3 px wider than its card: only the board scrolls sideways")
					.arg(who, QLatin1String(state)).arg(sideways));

				// The board is never cut off at the card's edge. When it is
				// wider than the card its own scroll shows a bar, below the
				// board and not over its last row.
				QScrollArea* boardScroll = qobject_cast<QScrollArea*>(view->parentWidget() != nullptr
					? view->parentWidget()->parentWidget() : nullptr);
				if (!check(boardScroll != nullptr,
					QStringLiteral("%1 board (%2) has no sideways scroll").arg(who, QLatin1String(state))))
					break;
				const QScrollBar* boardBar = boardScroll->horizontalScrollBar();
				qWarning("RoutingEditTest: board %s %s: view %dx%d (hint %dx%d), viewport %dx%d, sideways %d, bar %s",
					qPrintable(who), state, view->width(), view->height(), view->sizeHint().width(),
					view->sizeHint().height(), boardScroll->viewport()->width(), boardScroll->viewport()->height(),
					boardBar->maximum(), boardBar->isVisible() ? "shown" : "hidden");
				check(view->width() >= view->sizeHint().width(),
					QStringLiteral("%1 board (%2) is %3 px wide and needs %4")
					.arg(who, QLatin1String(state)).arg(view->width()).arg(view->sizeHint().width()));
				check(boardScroll->verticalScrollBar()->maximum() == 0 && boardScroll->viewport()->height() >= view->height(),
					QStringLiteral("%1 board (%2): %3 px of its height is under the bar or out of reach")
					.arg(who, QLatin1String(state)).arg(view->height() - boardScroll->viewport()->height()));
				if (view->width() > boardScroll->viewport()->width())
					check(boardBar->isVisible() && boardBar->maximum() > 0,
						QStringLiteral("%1 board (%2) is wider than its card and shows no bar")
						.arg(who, QLatin1String(state)));
				check(reach == 0,
					QStringLiteral("%1 body (%2) scrolls %3 px inside its card")
					.arg(who, QLatin1String(state)).arg(reach));
				check(body->widget()->height() <= body->widget()->sizeHint().height() + 1,
					QStringLiteral("%1 body (%2) is %3 px tall for %4 px of content")
					.arg(who, QLatin1String(state)).arg(body->widget()->height()).arg(body->widget()->sizeHint().height()));
				// The card follows its body. The body had the right height
				// while the row above it kept the old one, and the opened
				// board was cut off at the card's lower edge, with the control
				// that folds it again (Matrix and Minimal Copy cards).
				const int bodyBottom = body->mapTo(rows[0], QPoint(0, body->height())).y();
				check(bodyBottom <= rows[0]->height(),
					QStringLiteral("%1 body (%2) ends %3 px below its card, which is %4 px tall")
					.arg(who, QLatin1String(state)).arg(bodyBottom - rows[0]->height()).arg(rows[0]->height()));
			}
		}
	}

	// Shared routing commit contract, through each renderer's real editor:
	// rejected input must not notify the host, not just retain the bytes.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(name, true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy: VC=0.5*L") }, stereo, 0x3);
		RoutingView* view = rows.size() == 1 ? liveView(rows[0]) : nullptr;
		if (!check(view != nullptr, QStringLiteral("%1: routing commit view missing").arg(name)))
			continue;

		int notifications = 0;
		const QMetaObject::Connection connection = QObject::connect(view, &RoutingView::routingChanged,
			view, [&notifications]() { notifications++; });
		const auto commit = [&](const QString& text) {
			view->grab();
			QLineEdit* editor = nullptr;
			// Hit rectangles belong to the skin. Find its real double-click
			// target without duplicating that geometry in the gate or moving
			// the desktop pointer. No preceding press toggles a grid cell.
			for (int y = 2; y < view->height() && editor == nullptr; y += 4)
				for (int x = 2; x < view->width() && editor == nullptr; x += 4)
				{
					const QPointF local(x, y);
					QMouseEvent event(QEvent::MouseButtonDblClick, local,
						QPointF(view->mapToGlobal(local.toPoint())), Qt::LeftButton,
						Qt::LeftButton, Qt::NoModifier);
					QApplication::sendEvent(view, &event);
					for (QLineEdit* candidate : view->findChildren<QLineEdit*>())
						if (candidate->isVisible())
							editor = candidate;
				}
			if (!check(editor != nullptr, QStringLiteral("%1: routing editor did not open").arg(name)))
				return;
			editor->setText(text);
			pressEnter(editor);
			settle();
		};
		for (const QString& invalid : { QStringLiteral("inf"), QStringLiteral("bad*L") })
		{
			const QString before = lineText(scrollArea, 0);
			// A bare word is a channel in the step-list grammar, so qualify
			// infinity as a factor there rather than testing a valid name.
			commit(name == QLatin1String("minimal") && invalid == QLatin1String("inf")
				? QStringLiteral("inf*L") : invalid);
			check(notifications == 0, QStringLiteral("%1: rejected routing text emitted a change").arg(name));
			check(lineText(scrollArea, 0) == before,
				QStringLiteral("%1: rejected routing text changed the line").arg(name));
		}
		commit(QStringLiteral("INV"));
		check(notifications == 1, QStringLiteral("%1: INV did not emit exactly one change").arg(name));
		check(lineText(scrollArea, 0) == QStringLiteral("Copy: VC=-1.0*L"),
			QStringLiteral("%1: INV did not invert the source").arg(name));
		if (name == QLatin1String("flat") || name == QLatin1String("minimal"))
		{
			commit(QStringLiteral("L"));
			check(lineText(scrollArea, 0) == QStringLiteral("Copy: VC=L"),
				QStringLiteral("%1: re-entering a source did not restore unity").arg(name));
		}
		const int beforeAdd = notifications;
		const QString lineBeforeAdd = lineText(scrollArea, 0);
		for (const QString& channel : { QStringLiteral("NewBus"), QStringLiteral("newbus"), QStringLiteral("VC") })
		{
			view->galleryShowcase(QStringLiteral("addChannel"));
			QApplication::processEvents();
			QLineEdit* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
			if (!check(editor != nullptr, QStringLiteral("%1: channel editor missing").arg(name)))
				break;
			editor->setText(channel);
			pressEnter(editor);
			settle();
		}
		// Qt delivers routingChanged through the connected lambda during
		// pressEnter/settle; cppcheck cannot see that callback write.
		// cppcheck-suppress knownConditionTrueFalse
		check(notifications == beforeAdd && lineText(scrollArea, 0) == lineBeforeAdd,
			QStringLiteral("%1: empty channel additions changed the routing").arg(name));
		QObject::disconnect(connection);
	}

	// Part 3: ALL releases on the next pick, in every skin, on both cards.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(skin->id(), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Channel: ALL"), QStringLiteral("Device: all") }, stereo, 0x3);
		if (!check(rows.size() == 2, QStringLiteral("%1: expected 2 rows").arg(name)))
			continue;

		QToolButton* seat = nullptr;
		const QToolButton* allChannels = nullptr;
		for (QToolButton* button : rows[0]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("ChannelChip"))
				continue;
			if (button->property("allChannels").toBool())
				allChannels = button;
			else if (button->text() == QStringLiteral("R"))
				seat = button;
		}
		if (check(seat != nullptr && allChannels != nullptr, QStringLiteral("%1: Channel card chips not found").arg(name)))
		{
			check(allChannels->isChecked(), QStringLiteral("%1: ALL is not checked for 'Channel: ALL'").arg(name));
			check(seat->isEnabled(), QStringLiteral("%1: the R chip is inert while ALL is on").arg(name));
			seat->click();
			settle();
			check(lineText(scrollArea, 0) == QStringLiteral("Channel: R"),
				QStringLiteral("%1: after picking R the line is '%2'").arg(name, lineText(scrollArea, 0)));
			check(!allChannels->isChecked(), QStringLiteral("%1: ALL stayed checked after the pick").arg(name));
			check(seat->isChecked(), QStringLiteral("%1: the picked R chip is not checked").arg(name));
		}

		QToolButton* device = nullptr;
		const QToolButton* allDevices = nullptr;
		for (QToolButton* button : rows[1]->findChildren<QToolButton*>())
		{
			if (button->objectName() != QStringLiteral("DeviceChip"))
				continue;
			if (button->property("allDevices").toBool())
				allDevices = button;
			else if (device == nullptr && button->isVisible())
				device = button;
		}
		if (check(device != nullptr && allDevices != nullptr, QStringLiteral("%1: Device card chips not found").arg(name)))
		{
			check(allDevices->isChecked(), QStringLiteral("%1: All devices is not checked for 'Device: all'").arg(name));
			check(device->isEnabled(), QStringLiteral("%1: the device chip is inert while all is on").arg(name));
			device->click();
			settle();
			const QString line = lineText(scrollArea, 1);
			check(line.startsWith(QStringLiteral("Device: ")) && line != QStringLiteral("Device: all") && line.size() > 8,
				QStringLiteral("%1: after picking a device the line is '%2'").arg(name, line));
			check(!allDevices->isChecked(), QStringLiteral("%1: All devices stayed checked after the pick").arg(name));
			check(device->isChecked(), QStringLiteral("%1: the picked device chip is not checked").arg(name));
		}
	}

	// Part 4: a channel added with Enter in the Hilbert card's add field. The
	// field sits among the chips that Enter rebuilds, and the rebuild deleted
	// it while QLineEdit was still handling the key: the Editor crashed.
	{
		SkinManager::instance()->applySkin(QStringLiteral("studio"), true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Hilbert: Shift=L Align=R Direction=-90") }, stereo, 0x3);
		QLineEdit* add = rows.size() == 1 ? rows[0]->findChild<QLineEdit*>(QStringLiteral("ChannelChipAdd")) : nullptr;
		if (check(add != nullptr, QStringLiteral("Hilbert card: add-channel field not found")))
		{
			add->setText(QStringLiteral("VX"));
			pressEnter(add);
			settle();
			check(lineText(scrollArea, 0).contains(QStringLiteral("VX")),
				QStringLiteral("Hilbert card: the added channel is missing from '%1'").arg(lineText(scrollArea, 0)));
		}
	}

	// Part 5: the channel-mapping section of the Send and MultiConvolution
	// cards. It is open unless the user folded it. Folding is a view state:
	// the line stays as it is and the document is not marked edited. The
	// folded section says its routing in the line's own grammar, the focus
	// that was inside it goes to the fold button (not to another card), and a
	// rebuilt row (a skin switch, an undo) comes back in the state the user
	// left it in.
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		SkinManager::instance()->applySkin(name, true);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		const QList<QString> lines = {
			QStringLiteral("Send: {0d6a0000-0000-4000-8000-000000000002} L=L+0.5*R R=R Latency=20"),
			QStringLiteral("MultiConvolution: L=0+1 R=2+3 brir.wav") };
		const QStringList summaries = { QStringLiteral("L=L+0.5*R R=R"), QStringLiteral("L=0+1 R=2+3") };
		buildRows(scrollArea, configPath, lines, stereo, 0x3);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (!check(table != nullptr, QStringLiteral("%1: the routing fold table was not built").arg(name)))
			continue;
		const auto rowAt = [&](int index) -> FilterCardRow* {
			const QList<FilterCardRow*> current = table->findChildren<FilterCardRow*>(QString(), Qt::FindDirectChildrenOnly);
			return index < current.size() ? current.at(index) : nullptr;
		};
		// A MultiConvolution row builds its board when the file's channel count
		// arrives, a few event-loop passes after the row; the grid follows one
		// layout request at a time. Heights are read once the rows have stopped
		// changing.
		const auto relayout = [&]() {
			QList<int> heights;
			for (int pass = 0; pass < 20; pass++)
			{
				settle();
				if (table->layout() != nullptr)
					table->layout()->activate();
				QApplication::processEvents();
				QList<int> now;
				for (int index = 0; rowAt(index) != nullptr; index++)
					now.append(rowAt(index)->height());
				if (pass > 0 && now == heights)
					break;
				heights = now;
			}
		};
		// What assistive technology is given as a control's name, asked the
		// way the platform bridge asks.
		const auto spokenName = [](QWidget* widget) -> QString {
			QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(widget);
			return accessible != nullptr ? accessible->text(QAccessible::Name) : QString();
		};
		int edits = 0;
		QObject::connect(table, &FilterTable::linesChanged, table, [&edits]() { edits++; });
		scrollArea.activateWindow();
		relayout();

		for (int i = 0; i < lines.size(); i++)
		{
			const QString label = QStringLiteral("%1 %2").arg(name, lines.at(i).section(QLatin1Char(':'), 0, 0));
			FilterCardRow* row = rowAt(i);
			RoutingSectionHead* head = row != nullptr ? row->findChild<RoutingSectionHead*>() : nullptr;
			RoutingView* view = row != nullptr ? liveView(row) : nullptr;
			if (!check(head != nullptr && head->isVisibleTo(row) && view != nullptr,
				QStringLiteral("%1: no section head or no routing view").arg(label)))
				continue;
			check(!head->isFolded(), QStringLiteral("%1: the section starts folded").arg(label));

			// The row's bare-glyph buttons (the header's actions, the section's
			// fold arrow, MultiConvolution's "+") have a name in words. Qt hands
			// a button's accessible name to a screen reader, and without one its
			// text: "v", ">" and "+" say nothing when they are read out, and an
			// icon-only button has no name at all.
			for (QToolButton* button : row->findChildren<QToolButton*>(QStringLiteral("FilterCardIconButton")))
			{
				if (!button->isVisibleTo(row))
					continue;
				const QString spoken = spokenName(button);
				check(buttonLabelHasWords(spoken),
					QStringLiteral("%1: a button is named '%2' for a screen reader (text '%3', tooltip '%4')")
					.arg(label, spoken, button->text(), button->toolTip()));
			}
			const QString openFoldName = spokenName(head->foldButton());
			const int openHeight = row->height();
			const QString lineBefore = lineText(scrollArea, i);

			// The focus is inside the section when it folds.
			if (view->focusPolicy() == Qt::NoFocus)
				view->setFocusPolicy(Qt::StrongFocus);
			view->setFocus(Qt::OtherFocusReason);
			const bool focusInside = QApplication::focusWidget() == view;
			check(focusInside, QStringLiteral("%1: the routing view did not take the focus").arg(label));

			head->foldButton()->click();
			relayout();
			check(head->isFolded() && !view->isVisibleTo(row),
				QStringLiteral("%1: the section did not fold").arg(label));
			const int foldedHeight = row->height();
			check(foldedHeight < openHeight,
				QStringLiteral("%1: folded the card is %2 px, open it was %3 px").arg(label).arg(foldedHeight).arg(openHeight));
			check(head->summaryText() == summaries.at(i),
				QStringLiteral("%1: the folded summary is '%2' (expected '%3')").arg(label, head->summaryText(), summaries.at(i)));
			check(!focusInside || QApplication::focusWidget() == head->foldButton(),
				QStringLiteral("%1: folding moved the focus away from the section head").arg(label));
			check(buttonLabelHasWords(spokenName(head->foldButton())) && spokenName(head->foldButton()) != openFoldName,
				QStringLiteral("%1: the fold button is named '%2' folded and '%3' open for a screen reader")
				.arg(label, spokenName(head->foldButton()), openFoldName));
			check(lineText(scrollArea, i) == lineBefore && edits == 0,
				QStringLiteral("%1: folding changed the document ('%2', %3 edit(s))").arg(label, lineText(scrollArea, i)).arg(edits));

			// A rebuilt row keeps the fold the user chose.
			table->updateGuis();
			relayout();
			row = rowAt(i);
			head = row != nullptr ? row->findChild<RoutingSectionHead*>() : nullptr;
			if (!check(head != nullptr && head->isFolded(),
				QStringLiteral("%1: the fold did not survive a rebuilt row").arg(label)))
				continue;
			check(row->height() == foldedHeight,
				QStringLiteral("%1: the rebuilt folded card is %2 px (was %3 px)").arg(label).arg(row->height()).arg(foldedHeight));

			// Opened again, the board is back and the card is as tall as before.
			head->foldButton()->click();
			relayout();
			view = liveView(row);
			check(!head->isFolded() && view != nullptr,
				QStringLiteral("%1: the section did not open again").arg(label));
			check(row->height() == openHeight,
				QStringLiteral("%1: reopened the card is %2 px, it was %3 px").arg(label).arg(row->height()).arg(openHeight));

			// And a section the user opened again stays open.
			table->updateGuis();
			relayout();
			row = rowAt(i);
			head = row != nullptr ? row->findChild<RoutingSectionHead*>() : nullptr;
			check(head != nullptr && !head->isFolded(),
				QStringLiteral("%1: the reopened section folded again on a rebuilt row").arg(label));
			check(lineText(scrollArea, i) == lineBefore && edits == 0,
				QStringLiteral("%1: the fold round trip changed the document ('%2', %3 edit(s))")
				.arg(label, lineText(scrollArea, i)).arg(edits));
		}
	}

	qWarning("RoutingEditTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	return GalleryFixture::finish(failures == 0 ? 0 : 1);
}

int SkinGallery::runScrollBench()
{
	qWarning("ScrollBench: starting");
	const GalleryFixture fixture("ScrollBench");
	if (!fixture.isValid())
		return 2;

	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	for (ISkin* skin : Skins::all())
	{
		QScrollArea scrollArea;
		// A maximized QHD editor: the card column is ~2500 px wide.
		scrollArea.resize(2560, 1300);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, fixture.configPath(), lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
			continue;
		table->openConfig(QString());
		QApplication::processEvents();

		QScrollBar* bar = scrollArea.verticalScrollBar();
		bar->setValue(0);
		QApplication::processEvents();
		// Force real rasterization per step: grab() renders the viewport
		// through the same paint path the screen would use.
		QElapsedTimer timer;
		timer.start();
		int steps = 0;
		for (int value = 0; value <= bar->maximum() && steps < 40; value += 120, steps++)
		{
			bar->setValue(value);
			QApplication::processEvents();
			scrollArea.viewport()->grab();
		}
		const qint64 elapsed = timer.elapsed();
		qWarning("ScrollBench: %s %d steps in %lld ms (%.1f ms/step)",
			qPrintable(skin->id()), steps, static_cast<long long>(elapsed),
			steps > 0 ? double(elapsed) / steps : 0.0);
	}
	return GalleryFixture::finish(0);
}

int SkinGallery::runPowerToggleTest()
{
	qWarning("PowerToggleTest: starting");
	const GalleryFixture fixture("PowerToggleTest");
	if (!fixture.isValid())
		return 2;

	// Gain-less biquads (the field report) and full-grammar controls must
	// keep a real editor through load and an off/on round trip; a peaking
	// line missing its gain is one the ENGINE rejects ("no gain given"),
	// so its raw presentation is correct and only the text round trip is
	// held. Every line must survive the toggle byte-identically.
	struct Case { QString line; bool editor = true; };
	const QList<Case> cases = {
		{ QStringLiteral("Filter: ON NO Fc 800 Hz"), true },
		{ QStringLiteral("Filter: ON AP Fc 900 Hz BW Oct 1"), true },
		{ QStringLiteral("Filter: ON LP Fc 5000 Hz"), true },
		{ QStringLiteral("Filter: ON HPQ Fc 80 Hz Q 0.5"), true },
		{ QStringLiteral("Filter: ON BP Fc 1000 Hz Q 2"), true },
		{ QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"), true },
		{ QStringLiteral("Filter: ON PK Fc 1000 Hz Q 1"), false },
		{ QStringLiteral("Preamp: -3 dB"), true }
	};
	QList<QString> lines;
	for (const Case& c : cases)
		lines.append(c.line);

	int failures = 0;
	for (ISkin* skin : Skins::all())
	{
		const QString name = skin->id();
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		SkinManager::instance()->applySkin(skin->id(), true);
		buildRows(scrollArea, fixture.configPath(), lines);
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("PowerToggleTest: %s table construction failed", qPrintable(name));
			failures++;
			continue;
		}
		table->openConfig(QString());

		const auto rowWidget = [table](int index) -> FilterCardRow* {
			QList<FilterCardRow*> rows = table->findChildren<FilterCardRow*>(
				QString(), Qt::FindDirectChildrenOnly);
			std::sort(rows.begin(), rows.end(), [](FilterCardRow* a, FilterCardRow* b) {
				return a->y() < b->y();
			});
			return index < rows.size() ? rows[index] : nullptr;
		};
		// Found by the property the skins style it by: its tooltip now says
		// what a click does next, so it changes with the state.
		const auto powerButton = [](FilterCardRow* row) -> QToolButton* {
			for (QToolButton* button : row->findChildren<QToolButton*>())
			{
				if (button->property("powerToggle").toBool())
					return button;
			}
			return nullptr;
		};
		const auto settle = []() {
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
		};

		for (int i = 0; i < lines.size(); i++)
		{
			const QString original = table->documentItems().at(i)->text;
			if ((table->documentItems().at(i)->gui != nullptr) != cases[i].editor)
			{
				// The load-time half of the regression this gate pins: the
				// audit #275 B4 policy extraction dropped the card-to-chain
				// fallthrough and every plain biquad loaded as a raw row.
				qWarning("PowerToggleTest: %s row %d editor presence %d at load (expected %d)",
					qPrintable(name), i + 1,
					table->documentItems().at(i)->gui != nullptr ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			for (int phase = 0; phase < 2; phase++)
			{
				FilterCardRow* row = rowWidget(i);
				QToolButton* button = row != nullptr ? powerButton(row) : nullptr;
				if (button == nullptr)
				{
					qWarning("PowerToggleTest: %s row %d lost its power button in phase %d",
						qPrintable(name), i + 1, phase);
					failures++;
					break;
				}
				button->setChecked(phase == 1);
				settle();
				const QString text = table->documentItems().at(i)->text;
				const QString expected = phase == 0
					? QStringLiteral("# ") + original : original;
				if (text != expected)
				{
					qWarning("PowerToggleTest: %s row %d phase %d text '%s' (expected '%s')",
						qPrintable(name), i + 1, phase,
						qPrintable(text), qPrintable(expected));
					failures++;
				}
			}
			FilterCardRow* row = rowWidget(i);
			const bool hasGui = table->documentItems().at(i)->gui != nullptr;
			if (hasGui != cases[i].editor)
			{
				qWarning("PowerToggleTest: %s row %d editor presence %d after the toggle (expected %d)",
					qPrintable(name), i + 1, hasGui ? 1 : 0, cases[i].editor ? 1 : 0);
				failures++;
			}
			Q_UNUSED(row);
		}
	}
	qWarning("PowerToggleTest: %s (%d failure(s))", failures == 0 ? "PASS" : "FAIL", failures);
	return GalleryFixture::finish(failures == 0 ? 0 : 1);
}
