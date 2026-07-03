/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "HardwarePatchbayRoutingRenderer.h"

#include <QPainter>
#include <QMouseEvent>

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
	const bool dark = SkinManager::instance()->isDark();
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
		p.setPen(QPen(dark ? QColor(0x0C, 0x10, 0x13) : QColor(0xB8, 0xAC, 0x92), 1));
		p.drawLine(field.left() + 2, field.top(), field.right() - 2, field.top());
		p.setPen(QPen(dark ? QColor(0x3E, 0x47, 0x4F) : QColor(0xFF, 0xFF, 0xFF), 1));
		p.drawLine(field.left() + 2, field.bottom(), field.right() - 2, field.bottom());
	}

	// Engraved header labels: the faceplate's tracked lettering; channel
	// colour stays the cross-skin data ink.
	QFont label(t.monoFontFamily);
	label.setPixelSize(11);
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
	legend.setPixelSize(10);
	legend.setBold(true);

	for (int r = 0; r < matrix.outputs.size(); ++r)
	{
		const QString out = matrix.outputs[r];
		const QColor col(CopyRoutingAdapter::channelColor(out));
		p.setFont(label);
		p.setPen(col);
		p.drawText(QRect(0, colHeaderHeight + r * cellH, rowHeaderWidth - 11, cellH), Qt::AlignVCenter | Qt::AlignRight, out);

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
				face.setColorAt(0, dark ? QColor(0x2C, 0x33, 0x3A) : QColor(0xFF, 0xFF, 0xFF));
				face.setColorAt(1, dark ? QColor(0x1B, 0x21, 0x26) : QColor(0xE6, 0xDE, 0xCC));
				p.setBrush(face);
				p.setPen(QPen(dark ? QColor(0x11, 0x16, 0x1A) : QColor(0xAF, 0xA2, 0x88), 1));
				p.drawRoundedRect(cap, 2, 2);
				p.setPen(QPen(dark ? QColor(0x3E, 0x47, 0x4F) : QColor(0xFF, 0xFF, 0xFF), 1));
				p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
				p.setBrush(dark ? QColor(0x11, 0x16, 0x1A) : QColor(0xB8, 0xAC, 0x92));
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
				face.setColorAt(0, dark ? QColor(0x2A, 0x0E, 0x0C) : QColor(0xE8, 0xA6, 0x9E));
				face.setColorAt(1, dark ? QColor(0x4A, 0x1D, 0x1C) : QColor(0xF8, 0xD7, 0xD0));
				edge = QColor(t.danger);
				bevelTop = dark ? QColor(0x26, 0x08, 0x08) : QColor(0xA3, 0x40, 0x38);
				bevelBottom = dark ? QColor(0x7A, 0x2E, 0x2A) : QColor(0xFF, 0xE4, 0xDE);
				ink = dark ? QColor(0xFF, 0xD2, 0xCC) : QColor(0x5C, 0x12, 0x0C);
			}
			else
			{
				face.setColorAt(0, dark ? QColor(0x24, 0x1B, 0x0C) : QColor(0xE8, 0xC8, 0x87));
				face.setColorAt(1, dark ? QColor(0x4A, 0x3A, 0x1C) : QColor(0xFB, 0xE9, 0xC2));
				edge = QColor(t.accent);
				bevelTop = dark ? QColor(0x2A, 0x20, 0x08) : QColor(0xB9, 0x8F, 0x3E);
				bevelBottom = dark ? QColor(0x6E, 0x52, 0x1E) : QColor(0xFF, 0xF3, 0xD8);
				ink = dark ? QColor(0xFF, 0xE9, 0xC8) : QColor(0x4A, 0x2E, 0x00);
			}
			p.setBrush(face);
			p.setPen(QPen(edge, 1));
			p.drawRoundedRect(cap, 2, 2);
			p.setPen(QPen(bevelTop, 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
			p.setPen(QPen(bevelBottom, 1));
			p.drawLine(cap.left() + 2, cap.bottom() - 1, cap.right() - 2, cap.bottom() - 1);

			if (portModel.allowFactors)
			{
				// The gain is the button legend, lit by the lamp under the cap.
				QString capText;
				if (cell.factor == -1.0 && !cell.isDecibel)
					capText = QStringLiteral("INV");
				else if (cell.factor == 1.0 && !cell.isDecibel)
					capText = QStringLiteral("0dB");
				else
					capText = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QStringLiteral("x%1").arg(cell.factor);
				p.setFont(legend);
				p.setPen(ink);
				p.drawText(cap, Qt::AlignCenter, capText);
			}
			else
			{
				// A factor-less patch point (MultiConvolution) is just patched
				// or not: a blank cap with the lamp window glowing in it.
				p.setBrush(a8(ink, 230));
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(QRect(cap.center().x() - 6, cap.center().y() - 2, 12, 4), 2, 2);
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
