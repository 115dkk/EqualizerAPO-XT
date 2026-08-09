/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "CrosspointMatrixRoutingRenderer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"

using std::vector;

CrosspointMatrixView::CrosspointMatrixView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent)
	: RoutingView(parent),
	// Seeding every device channel as a row keeps the grid editable even when
	// the command references few (or no) channels; without it an emptied Copy
	// could never be refilled from the GUI. Empty rows are skipped by the
	// serializer, so the config line is unaffected. The fold below decides
	// which seeded rows actually reach the board.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced stay on the board for the whole session,
	// even if their last source is toggled away.
	pinnedChannels(RoutingFold::referencedTargets(assignments))
{
	setMouseTracking(true);
	// Match the stable painted-routing-view size contract (StepList / BlockChip):
	// horizontal policy Ignored + minimumSizeHint == sizeHint. Deviating from
	// this (Preferred + zero minimum) made Qt's layout geometry pass crash flakily
	// on the maximized initial show.
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	rebuildMatrix();
}

void CrosspointMatrixView::rebuildMatrix()
{
	fold = RoutingFold::fold(workingAssignments, deviceChannels, pinnedChannels,
		channelsExpanded, portModel.fixedSources);
	rowMap = fold.visibleRows;
	vector<Assignment> visible;
	visible.reserve(rowMap.size());
	for (int row : rowMap)
		visible.push_back(workingAssignments[row]);
	matrix = CopyRoutingAdapter::buildMatrix(visible, fold.inputs);
	updateMetrics();
	syncSizeToHint();
	update();
}

void CrosspointMatrixView::updateMetrics()
{
	// The stock cell widths fit a Copy row's short device names (L, R,
	// SL, ...). The subwoofer crossover dialog posts channels like
	// FrontBass and SourceLFE on the same board, and a pill narrower than
	// its caption clipped it to fragments ("rontBas"). The caption font
	// decides the width a column really needs.
	QFont monoFont(SkinManager::instance()->tokens().monoFontFamily);
	monoFont.setPixelSize(11);
	const QFontMetrics fm(monoFont);

	int longestInput = 0;
	for (const QString& ch : matrix.inputs)
		longestInput = qMax(longestInput, fm.horizontalAdvance(ch));
	int longestOutput = 0;
	for (const QString& out : matrix.outputs)
		longestOutput = qMax(longestOutput, fm.horizontalAdvance(out));

	// Column pills inset the header cell by 6+6, row pills by 6+8, and the
	// caption keeps a little air inside the pill.
	cellW = qMax(52, longestInput + 20);
	rowHeaderWidth = qMax(64, longestOutput + 22);
}

std::vector<Assignment> CrosspointMatrixView::assignments() const
{
	return workingAssignments;
}

void CrosspointMatrixView::galleryShowcase(const QString& state)
{
	if (state == QLatin1String("expanded"))
	{
		channelsExpanded = true;
		rebuildMatrix();
	}
	else if (state == QLatin1String("addChannel"))
	{
		openChannelEditor();
		if (channelEditor != nullptr)
			channelEditor->setText(QStringLiteral("VS"));
	}
}

Assignment& CrosspointMatrixView::rowAssignment(int outRow)
{
	return workingAssignments[rowMap[outRow]];
}

int CrosspointMatrixView::summandIndex(int outRow, const QString& channel) const
{
	if (outRow < 0 || outRow >= rowMap.size())
		return -1;
	const Assignment& a = workingAssignments[rowMap[outRow]];
	for (int i = 0; i < (int)a.sourceSum.size(); ++i)
		if (QString::fromStdWString(a.sourceSum[i].channel) == channel)
			return i;
	return -1;
}

QRect CrosspointMatrixView::cellRect(int outRow, int inCol) const
{
	return QRect(rowHeaderWidth + inCol * cellW, colHeaderHeight + outRow * cellH, cellW, cellH);
}

QRect CrosspointMatrixView::footerRect() const
{
	return QRect(0, colHeaderHeight + matrix.outputs.size() * cellH + 4, width(), footerH);
}

bool CrosspointMatrixView::hitTest(const QPoint& pos, int& outRow, int& inCol) const
{
	if (pos.x() < rowHeaderWidth || pos.y() < colHeaderHeight)
		return false;
	inCol = (pos.x() - rowHeaderWidth) / cellW;
	outRow = (pos.y() - colHeaderHeight) / cellH;
	return outRow >= 0 && outRow < matrix.outputs.size() && inCol >= 0 && inCol < matrix.inputs.size();
}

QSize CrosspointMatrixView::sizeHint() const
{
	int w = rowHeaderWidth + matrix.inputs.size() * cellW + 2;
	int h = colHeaderHeight + matrix.outputs.size() * cellH + 2;
	// The footer's caption cells need their own width when the grid above
	// is narrow (an emptied deviceless Copy has no columns at all).
	h += 4 + footerH;
	w = qMax(w, 220);
	return QSize(w, h);
}

QSize CrosspointMatrixView::minimumSizeHint() const
{
	// Same as the stable StepList/BlockChip views: report the full content size.
	// The host QScrollArea (its own minimum pinned to 0) isolates this, so the
	// FilterTable grid column is not inflated.
	return sizeHint();
}

static QColor mix(const QColor& c, int alpha)
{
	QColor r = c;
	r.setAlpha(alpha);
	return r;
}

void CrosspointMatrixView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor text(t.text);
	const QColor muted(t.mutedText);
	const QColor border(t.border);
	const QColor accent(t.accent);
	const QColor ok(t.success);
	const QColor danger(t.danger);

	QFont monoFont(t.monoFontFamily);
	monoFont.setPixelSize(11);
	p.setFont(monoFont);

	removeRects.clear();
	removeRects.resize(matrix.outputs.size());

	// Column headers (input channels; in fixed-source mode these are the IR
	// file's channel numbers, which are ports rather than virtual channels, so
	// they get the solid pill instead of the dashed virtual styling).
	for (int c = 0; c < matrix.inputs.size(); ++c)
	{
		const QString ch = matrix.inputs[c];
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		const QRect hr(rowHeaderWidth + c * cellW, 0, cellW, colHeaderHeight);
		const bool virt = !portModel.fixedSourceMode() && CopyRoutingAdapter::isVirtualChannel(ch);
		QRect pill = hr.adjusted(6, 6, -6, -8);
		if (virt)
		{
			p.setPen(QPen(mix(col, 170), 1, Qt::DashLine));
			p.setBrush(mix(col, 28));
		}
		else
		{
			p.setPen(Qt::NoPen);
			p.setBrush(col);
		}
		p.drawRect(pill);
		p.setPen(virt ? col : QColor(Qt::white));
		p.drawText(pill, Qt::AlignCenter, ch);
	}

	// Rows.
	for (int r = 0; r < matrix.outputs.size(); ++r)
	{
		const QString out = matrix.outputs[r];
		const QColor col(CopyRoutingAdapter::channelColor(out));
		const QRect rr(0, colHeaderHeight + r * cellH, rowHeaderWidth, cellH);
		const bool virt = CopyRoutingAdapter::isVirtualChannel(out);
		QRect pill = rr.adjusted(6, 4, -8, -4);
		if (virt)
		{
			p.setPen(QPen(mix(col, 170), 1, Qt::DashLine));
			p.setBrush(mix(col, 28));
		}
		else
		{
			p.setPen(Qt::NoPen);
			p.setBrush(col);
		}
		p.drawRect(pill);
		p.setPen(virt ? col : QColor(Qt::white));
		p.drawText(pill, Qt::AlignCenter, out);

		// A virtual bus can leave the board: hovering its row exposes an x
		// target on the header pill (physical channels fold instead of
		// leaving, so they never get one).
		if (virt && hoveredRow == r)
		{
			const QRect xr(pill.right() - 14, pill.top(), 14, pill.height());
			p.setPen(mix(col, 220));
			p.drawText(xr, Qt::AlignCenter, QStringLiteral("x"));
			removeRects[r] = xr;
		}

		// Cells.
		for (int c = 0; c < matrix.inputs.size(); ++c)
		{
			const QRect cr = cellRect(r, c).adjusted(1, 1, -1, -1);
			const CopyRoutingAdapter::Cell cell = matrix.cell(r, c);

			p.setPen(QPen(border, 1));
			if (!cell.present)
			{
				p.setBrush(mix(border, 18));
				p.drawRect(cr);
				continue;
			}

			QColor fill;
			QString label;
			const bool unity = cell.factor == 1.0 && !cell.isDecibel;
			if (!portModel.allowFactors)
			{
				// Factor-less routing (MultiConvolution): a crosspoint is either
				// patched or not, so the cell shows a connection dot, not a gain.
				fill = ok;
				label = QStringLiteral("●");
			}
			else if (cell.factor < 0)
			{
				fill = danger;
				label = cell.factor == -1.0 && !cell.isDecibel ? QStringLiteral("INV") : QString::number(cell.factor);
			}
			else if (unity)
			{
				fill = ok;
				label = QStringLiteral("1");
			}
			else
			{
				fill = accent;
				label = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor);
			}

			p.setBrush(mix(fill, 60));
			p.setPen(QPen(mix(fill, 200), 1));
			p.drawRect(cr);
			p.setPen(text);
			p.drawText(cr, Qt::AlignCenter, label);
		}
	}

	// Footer strip: the board's expansion controls as mono caption KEYS. At
	// rest they wear the exact resting treatment of an empty crosspoint cell
	// (1px border rule + faint fill) - the pressable-cell form the grid above
	// has already taught - because a bare transparent caption does not read as
	// a button at all. Hover is the accent prelight.
	// +N CH reveals the folded device channels, +BUS patches a new virtual
	// bus onto the board.
	revealRect = QRect();
	addRect = QRect();
	const QRect footer = footerRect();
	QFontMetrics fm(monoFont);
	int x = 4;

	auto captionCell = [&](const QString& caption, bool hovered) {
		const int w = fm.horizontalAdvance(caption) + 16;
		const QRect cr(x, footer.top(), w, footer.height());
		const QRect cell = cr.adjusted(0, 1, -1, -2);
		if (hovered)
		{
			p.setPen(QPen(mix(accent, 200), 1));
			p.setBrush(mix(accent, 24));
		}
		else
		{
			p.setPen(QPen(border, 1));
			p.setBrush(mix(border, 18));
		}
		p.drawRect(cell);
		p.setPen(hovered ? text : muted);
		p.drawText(cr, Qt::AlignCenter, caption);
		x += w + 8;
		return cr;
	};

	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString caption = channelsExpanded
			? QStringLiteral("FOLD")
			: QStringLiteral("+%1 CH").arg(fold.hiddenChannels);
		revealRect = captionCell(caption, hoveredControl == 1);
	}
	addRect = captionCell(QStringLiteral("+BUS"), hoveredControl == 2);
}

void CrosspointMatrixView::mousePressEvent(QMouseEvent* event)
{
	for (int r = 0; r < removeRects.size(); ++r)
	{
		if (!removeRects[r].isNull() && removeRects[r].contains(event->pos()))
		{
			const QString channel = matrix.outputs[r];
			for (int i = pinnedChannels.size() - 1; i >= 0; i--)
				if (pinnedChannels[i].compare(channel, Qt::CaseInsensitive) == 0)
					pinnedChannels.removeAt(i);
			const bool changed = RoutingFold::removeChannel(workingAssignments, channel);
			rebuildMatrix();
			if (changed)
				emit routingChanged();
			return;
		}
	}
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		rebuildMatrix();
		return;
	}
	if (!addRect.isNull() && addRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}

	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	const QString channel = matrix.inputs[inCol];
	const int idx = summandIndex(outRow, channel);
	Assignment& a = rowAssignment(outRow);
	if (idx >= 0)
	{
		// Toggle off.
		a.sourceSum.erase(a.sourceSum.begin() + idx);
	}
	else
	{
		// Toggle on at unity gain.
		Assignment::Summand s;
		s.factor = 1.0;
		s.isDecibel = false;
		s.channel = channel.toStdWString();
		a.sourceSum.push_back(s);
	}
	rebuildMatrix();
	emit routingChanged();
}

void CrosspointMatrixView::mouseMoveEvent(QMouseEvent* event)
{
	int control = 0;
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
		control = 1;
	else if (!addRect.isNull() && addRect.contains(event->pos()))
		control = 2;

	int row = -1;
	if (event->pos().x() < rowHeaderWidth && event->pos().y() >= colHeaderHeight)
	{
		const int r = (event->pos().y() - colHeaderHeight) / cellH;
		if (r >= 0 && r < matrix.outputs.size())
			row = r;
	}

	if (control != hoveredControl || row != hoveredRow)
	{
		hoveredControl = control;
		hoveredRow = row;
		bool removable = false;
		if (row >= 0 && !removeRects.value(row).isNull()
			&& removeRects[row].contains(event->pos()))
			removable = true;
		setCursor(control != 0 || removable ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
	RoutingView::mouseMoveEvent(event);
}

void CrosspointMatrixView::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
}

void CrosspointMatrixView::mouseDoubleClickEvent(QMouseEvent* event)
{
	// Without factors there is nothing to edit; the press that preceded this
	// double-click already toggled the crosspoint.
	if (!portModel.allowFactors)
		return;

	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	commitEditor();
	editRow = outRow;
	editCol = inCol;

	const CopyRoutingAdapter::Cell cell = matrix.cell(outRow, inCol);
	QString textValue;
	if (cell.present)
		textValue = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor);
	else
		textValue = QStringLiteral("1");

	if (editor == nullptr)
	{
		editor = new QLineEdit(this);
		editor->setObjectName(QStringLiteral("CrosspointEditor"));
		editor->setAlignment(Qt::AlignCenter);
		connect(editor, &QLineEdit::editingFinished, this, &CrosspointMatrixView::commitEditor);
	}
	editor->setGeometry(cellRect(outRow, inCol));
	editor->setText(textValue);
	editor->show();
	editor->setFocus();
	editor->selectAll();
}

void CrosspointMatrixView::commitEditor()
{
	if (editor == nullptr || !editor->isVisible() || editRow < 0)
		return;

	const int outRow = editRow;
	const QString channel = matrix.inputs.value(editCol);
	editRow = editCol = -1;
	QString raw = editor->text().trimmed();
	editor->hide();

	if (channel.isEmpty() || outRow >= rowMap.size())
		return;

	Assignment& a = rowAssignment(outRow);
	const int idx = summandIndex(outRow, channel);

	if (raw.isEmpty())
	{
		if (idx >= 0)
			a.sourceSum.erase(a.sourceSum.begin() + idx);
		rebuildMatrix();
		emit routingChanged();
		return;
	}

	Assignment::Summand parsed;
	if (!CopyRoutingAdapter::parseFactorToken(raw, parsed))
		return;

	if (idx >= 0)
	{
		a.sourceSum[idx].factor = parsed.factor;
		a.sourceSum[idx].isDecibel = parsed.isDecibel;
	}
	else
	{
		Assignment::Summand s;
		s.factor = parsed.factor;
		s.isDecibel = parsed.isDecibel;
		s.channel = channel.toStdWString();
		a.sourceSum.push_back(s);
	}
	rebuildMatrix();
	emit routingChanged();
}

void CrosspointMatrixView::openChannelEditor()
{
	if (channelEditor == nullptr)
	{
		channelEditor = new QLineEdit(this);
		channelEditor->setObjectName(QStringLiteral("CrosspointChannelEditor"));
		channelEditor->setAlignment(Qt::AlignCenter);
		connect(channelEditor, &QLineEdit::editingFinished, this, &CrosspointMatrixView::commitChannelEditor);
	}
	channelEditor->setGeometry(addRect.isNull()
		? QRect(4, footerRect().top(), 96, footerH)
		: addRect.adjusted(0, 0, 64, 0));
	channelEditor->setText(QString());
	channelEditor->show();
	channelEditor->setFocus();
}

void CrosspointMatrixView::commitChannelEditor()
{
	if (channelEditor == nullptr || !channelEditor->isVisible())
		return;

	const QString name = channelEditor->text().trimmed();
	channelEditor->hide();
	if (!RoutingFold::isValidChannelName(name))
		return;

	// An existing channel just gets pinned onto the board; a new name becomes
	// a virtual bus row. No routingChanged: a fresh target has no sum yet and
	// the serializer skips empty targets.
	CopyRoutingAdapter::ensureTargetChannel(workingAssignments, pinnedChannels, name);
	rebuildMatrix();
}

RoutingView* CrosspointMatrixRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent)
{
	return new CrosspointMatrixView(assignments, channelNames, portModel, parent);
}
