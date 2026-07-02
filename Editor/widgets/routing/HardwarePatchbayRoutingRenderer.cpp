/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "HardwarePatchbayRoutingRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>

#include "Editor/SkinManager.h"

using std::vector;

HardwarePatchbayView::HardwarePatchbayView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent)
	: RoutingView(parent),
	// Seed every device channel as a row/column so an emptied Copy can be
	// refilled from the GUI; empty rows are skipped by the serializer.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel)
{
	// Match the stable painted-routing-view size contract (StepList / BlockChip).
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	rebuildMatrix();
}

void HardwarePatchbayView::rebuildMatrix()
{
	matrix = portModel.fixedSourceMode()
		? CopyRoutingAdapter::buildMatrix(workingAssignments, portModel.fixedSources)
		: CopyRoutingAdapter::buildMatrix(workingAssignments, deviceChannels);
	updateGeometry();
	update();
}

std::vector<Assignment> HardwarePatchbayView::assignments() const
{
	return workingAssignments;
}

int HardwarePatchbayView::summandIndex(int outRow, const QString& channel) const
{
	if (outRow < 0 || outRow >= (int)workingAssignments.size())
		return -1;
	const Assignment& a = workingAssignments[outRow];
	for (int i = 0; i < (int)a.sourceSum.size(); ++i)
		if (QString::fromStdWString(a.sourceSum[i].channel) == channel)
			return i;
	return -1;
}

QRect HardwarePatchbayView::cellRect(int outRow, int inCol) const
{
	return QRect(rowHeaderWidth + inCol * cellW, colHeaderHeight + outRow * cellH, cellW, cellH);
}

bool HardwarePatchbayView::hitTest(const QPoint& pos, int& outRow, int& inCol) const
{
	if (pos.x() < rowHeaderWidth || pos.y() < colHeaderHeight)
		return false;
	inCol = (pos.x() - rowHeaderWidth) / cellW;
	outRow = (pos.y() - colHeaderHeight) / cellH;
	return outRow >= 0 && outRow < matrix.outputs.size() && inCol >= 0 && inCol < matrix.inputs.size();
}

QSize HardwarePatchbayView::sizeHint() const
{
	return QSize(rowHeaderWidth + matrix.inputs.size() * cellW + 8,
		colHeaderHeight + matrix.outputs.size() * cellH + 8);
}

QSize HardwarePatchbayView::minimumSizeHint() const
{
	return sizeHint();
}

static QColor a8(const QColor& c, int a) { QColor r = c; r.setAlpha(a); return r; }

void HardwarePatchbayView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const QColor text(t.text), muted(t.mutedText), border(t.border);
	const QColor accent(t.accent), card(t.card);

	// Brushed metal panel background.
	QLinearGradient panel(0, 0, 0, height());
	panel.setColorAt(0, QColor(t.surface).lighter(108));
	panel.setColorAt(1, QColor(t.surface).darker(108));
	p.fillRect(rect(), panel);

	QFont label(t.monoFontFamily);
	label.setPixelSize(11);
	p.setFont(label);

	// Column (input) engraved labels.
	for (int c = 0; c < matrix.inputs.size(); ++c)
	{
		const QString ch = matrix.inputs[c];
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		p.setPen(col);
		p.drawText(QRect(rowHeaderWidth + c * cellW, 0, cellW, colHeaderHeight), Qt::AlignCenter, ch);
	}

	for (int r = 0; r < matrix.outputs.size(); ++r)
	{
		const QString out = matrix.outputs[r];
		const QColor col(CopyRoutingAdapter::channelColor(out));
		p.setPen(col);
		p.drawText(QRect(0, colHeaderHeight + r * cellH, rowHeaderWidth - 6, cellH), Qt::AlignVCenter | Qt::AlignRight, out);

		for (int c = 0; c < matrix.inputs.size(); ++c)
		{
			const QRect cr = cellRect(r, c);
			const CopyRoutingAdapter::Cell cell = matrix.cell(r, c);
			const QPointF center = cr.center();
			const int radius = 15;

			// Knob base.
			QRadialGradient knob(center, radius);
			knob.setColorAt(0, QColor(card).lighter(cell.present ? 130 : 105));
			knob.setColorAt(1, QColor(card).darker(140));
			p.setBrush(knob);
			p.setPen(QPen(a8(border, 200), 1));
			p.drawEllipse(center, radius, radius);

			if (cell.present)
			{
				// Lit ring + pointer indicating gain.
				const QColor lit = cell.factor < 0 ? QColor(t.danger) : accent;
				p.setPen(QPen(lit, 2));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(center, radius - 2, radius - 2);

				// Pointer angle: map factor (-1..+1.5 -> -135..+135 deg).
				double norm = qBound(-1.0, cell.factor, 1.5) / 1.5;
				double angle = (-90.0 - norm * 135.0) * M_PI / 180.0;
				QPointF tip(center.x() + qCos(angle) * (radius - 4), center.y() - qSin(angle) * (radius - 4));
				p.setPen(QPen(lit, 2));
				p.drawLine(center, tip);

				// Value caption under the knob. A factor-less patch point
				// (MultiConvolution) is just patched or not, so no gain caption.
				if (portModel.allowFactors)
				{
					QString cap;
					if (cell.factor == -1.0 && !cell.isDecibel)
						cap = QStringLiteral("INV");
					else if (cell.factor == 1.0 && !cell.isDecibel)
						cap = QStringLiteral("0dB");
					else
						cap = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QStringLiteral("x%1").arg(cell.factor);
					p.setPen(text);
					p.drawText(QRect(cr.left(), cr.bottom() - 12, cr.width(), 12), Qt::AlignCenter, cap);
				}
			}
			else
			{
				p.setPen(a8(muted, 120));
				p.drawText(cr, Qt::AlignCenter, QStringLiteral("·"));
			}
		}
	}
}

void HardwarePatchbayView::mousePressEvent(QMouseEvent* event)
{
	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	const QString channel = matrix.inputs[inCol];
	const int idx = summandIndex(outRow, channel);
	Assignment& a = workingAssignments[outRow];
	if (idx >= 0)
		a.sourceSum.erase(a.sourceSum.begin() + idx);
	else
	{
		Assignment::Summand s;
		s.factor = 1.0;
		s.isDecibel = false;
		s.channel = channel.toStdWString();
		a.sourceSum.push_back(s);
	}
	rebuildMatrix();
	emit routingChanged();
}

void HardwarePatchbayView::mouseDoubleClickEvent(QMouseEvent* event)
{
	// Without factors there is nothing to edit; the press that preceded this
	// double-click already toggled the patch point.
	if (!portModel.allowFactors)
		return;

	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	commitEditor();
	editRow = outRow;
	editCol = inCol;

	const CopyRoutingAdapter::Cell cell = matrix.cell(outRow, inCol);
	const QString textValue = cell.present
		? (cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor))
		: QStringLiteral("1");

	if (editor == nullptr)
	{
		editor = new QLineEdit(this);
		editor->setObjectName(QStringLiteral("PatchbayEditor"));
		editor->setAlignment(Qt::AlignCenter);
		connect(editor, &QLineEdit::editingFinished, this, &HardwarePatchbayView::commitEditor);
	}
	const QRect cr = cellRect(outRow, inCol);
	editor->setGeometry(QRect(cr.left() + 6, cr.center().y() - 10, cr.width() - 12, 20));
	editor->setText(textValue);
	editor->show();
	editor->setFocus();
	editor->selectAll();
}

void HardwarePatchbayView::commitEditor()
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
		factor = -1.0;
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

RoutingView* HardwarePatchbayRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent)
{
	return new HardwarePatchbayView(assignments, channelNames, portModel, parent);
}
