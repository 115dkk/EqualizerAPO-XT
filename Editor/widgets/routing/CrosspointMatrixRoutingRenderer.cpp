/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "CrosspointMatrixRoutingRenderer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"

using std::vector;

CrosspointMatrixView::CrosspointMatrixView(const vector<Assignment>& assignments, QWidget* parent)
	: RoutingView(parent), workingAssignments(assignments)
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
	matrix = CopyRoutingAdapter::buildMatrix(workingAssignments);
	updateGeometry();
	update();
}

std::vector<Assignment> CrosspointMatrixView::assignments() const
{
	return workingAssignments;
}

int CrosspointMatrixView::summandIndex(int outRow, const QString& channel) const
{
	if (outRow < 0 || outRow >= (int)workingAssignments.size())
		return -1;
	const Assignment& a = workingAssignments[outRow];
	for (int i = 0; i < (int)a.sourceSum.size(); ++i)
		if (QString::fromStdWString(a.sourceSum[i].channel) == channel)
			return i;
	return -1;
}

QRect CrosspointMatrixView::cellRect(int outRow, int inCol) const
{
	return QRect(rowHeaderWidth + inCol * cellW, colHeaderHeight + outRow * cellH, cellW, cellH);
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
	const int w = rowHeaderWidth + matrix.inputs.size() * cellW + 2;
	const int h = colHeaderHeight + matrix.outputs.size() * cellH + 2;
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

	// Column headers (input channels).
	for (int c = 0; c < matrix.inputs.size(); ++c)
	{
		const QString ch = matrix.inputs[c];
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		const QRect hr(rowHeaderWidth + c * cellW, 0, cellW, colHeaderHeight);
		const bool virt = CopyRoutingAdapter::isVirtualChannel(ch);
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
			if (cell.factor < 0)
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
}

void CrosspointMatrixView::mousePressEvent(QMouseEvent* event)
{
	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	const QString channel = matrix.inputs[inCol];
	const int idx = summandIndex(outRow, channel);
	Assignment& a = workingAssignments[outRow];
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

void CrosspointMatrixView::mouseDoubleClickEvent(QMouseEvent* event)
{
	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	commitEditor();
	editRow = outRow;
	editCol = inCol;

	const QString channel = matrix.inputs[inCol];
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

	if (channel.isEmpty())
		return;

	Assignment& a = workingAssignments[outRow];
	const int idx = summandIndex(outRow, channel);

	if (raw.isEmpty())
	{
		if (idx >= 0)
			a.sourceSum.erase(a.sourceSum.begin() + idx);
		rebuildMatrix();
		emit routingChanged();
		return;
	}

	double factor = 1.0;
	bool isDecibel = false;
	if (raw.compare(QLatin1String("INV"), Qt::CaseInsensitive) == 0)
	{
		factor = -1.0;
	}
	else
	{
		QString num = raw;
		if (num.right(2).toLower() == QLatin1String("db"))
		{
			isDecibel = true;
			num = num.left(num.size() - 2).trimmed();
		}
		bool ok = false;
		const double parsed = num.toDouble(&ok);
		if (ok)
			factor = parsed;
	}

	if (idx >= 0)
	{
		a.sourceSum[idx].factor = factor;
		a.sourceSum[idx].isDecibel = isDecibel;
	}
	else
	{
		Assignment::Summand s;
		s.factor = factor;
		s.isDecibel = isDecibel;
		s.channel = channel.toStdWString();
		a.sourceSum.push_back(s);
	}
	rebuildMatrix();
	emit routingChanged();
}

RoutingView* CrosspointMatrixRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& /*channelNames*/, QWidget* parent)
{
	return new CrosspointMatrixView(assignments, parent);
}
