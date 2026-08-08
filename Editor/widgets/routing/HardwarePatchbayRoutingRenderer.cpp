/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "HardwarePatchbayRoutingRenderer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"

using std::vector;

HardwarePatchbayView::HardwarePatchbayView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent)
	: RoutingView(parent),
	// Seed every device channel as a row so an emptied Copy can be refilled
	// from the GUI; empty rows are skipped by the serializer. The fold below
	// decides which seeded rows are actually mounted on the panel.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced stay mounted for the whole session,
	// even if their last source is toggled away.
	pinnedChannels(RoutingFold::referencedTargets(assignments))
{
	// Match the stable painted-routing-view size contract (StepList / BlockChip).
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	rebuildMatrix();
}

void HardwarePatchbayView::rebuildMatrix()
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

void HardwarePatchbayView::updateMetrics()
{
	// The stock faceplate widths fit the short device names of a Copy row
	// (L, R, SL, ...). The subwoofer crossover dialog mounts channels like
	// FrontBass and SourceLFE on the same panel, and an engraved label
	// longer than its column was clipped to fragments ("ntBass"). The
	// engraving font decides the width a column really needs.
	QFont label(SkinManager::instance()->tokens().monoFontFamily);
	label.setPixelSize(11);
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

std::vector<Assignment> HardwarePatchbayView::assignments() const
{
	return workingAssignments;
}

void HardwarePatchbayView::galleryShowcase(const QString& state)
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

Assignment& HardwarePatchbayView::rowAssignment(int outRow)
{
	return workingAssignments[rowMap[outRow]];
}

int HardwarePatchbayView::summandIndex(int outRow, const QString& channel) const
{
	if (outRow < 0 || outRow >= rowMap.size())
		return -1;
	const Assignment& a = workingAssignments[rowMap[outRow]];
	for (int i = 0; i < (int)a.sourceSum.size(); ++i)
		if (QString::fromStdWString(a.sourceSum[i].channel) == channel)
			return i;
	return -1;
}

QRect HardwarePatchbayView::cellRect(int outRow, int inCol) const
{
	return QRect(rowHeaderWidth + inCol * cellW, colHeaderHeight + outRow * cellH, cellW, cellH);
}

QRect HardwarePatchbayView::stripRect() const
{
	return QRect(0, colHeaderHeight + matrix.outputs.size() * cellH + 10, width(), stripH);
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
	int w = rowHeaderWidth + matrix.inputs.size() * cellW + 8;
	int h = colHeaderHeight + matrix.outputs.size() * cellH + 8;
	h += 10 + stripH + 4;
	w = qMax(w, 240);
	return QSize(w, h);
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

	// The control-cap recipe, shared by the crosspoint blanks and the
	// faceplate strip buttons: raised = lit top bevel, latched = inverted
	// bevel with the face dropped 1px (the machine's one latch law).
	auto drawCap = [&](QRect cap, bool latched, const QString& capText, bool prelit) {
		if (latched)
			cap.translate(0, 1);
		QLinearGradient face(cap.topLeft(), cap.bottomLeft());
		if (latched)
		{
			face.setColorAt(0, dark ? QColor(0x16, 0x1B, 0x20) : QColor(0xD9, 0xD0, 0xBA));
			face.setColorAt(1, dark ? QColor(0x21, 0x27, 0x2D) : QColor(0xEE, 0xE7, 0xD4));
		}
		else
		{
			face.setColorAt(0, dark ? QColor(prelit ? 0x34 : 0x2C, prelit ? 0x3C : 0x33, prelit ? 0x44 : 0x3A)
				: (prelit ? QColor(0xFF, 0xFF, 0xFF) : QColor(0xFB, 0xF7, 0xEC)));
			face.setColorAt(1, dark ? QColor(0x1B, 0x21, 0x26) : QColor(0xE6, 0xDE, 0xCC));
		}
		p.setBrush(face);
		p.setPen(QPen(dark ? QColor(0x11, 0x16, 0x1A) : QColor(0xAF, 0xA2, 0x88), 1));
		p.drawRoundedRect(cap, 2, 2);
		if (latched)
		{
			p.setPen(QPen(dark ? QColor(0x0C, 0x10, 0x13) : QColor(0xB8, 0xAC, 0x92), 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
			p.setPen(QPen(dark ? QColor(0x3E, 0x47, 0x4F) : QColor(0xFF, 0xFF, 0xFF), 1));
			p.drawLine(cap.left() + 2, cap.bottom() - 1, cap.right() - 2, cap.bottom() - 1);
		}
		else
		{
			p.setPen(QPen(dark ? QColor(0x3E, 0x47, 0x4F) : QColor(0xFF, 0xFF, 0xFF), 1));
			p.drawLine(cap.left() + 2, cap.top() + 1, cap.right() - 2, cap.top() + 1);
		}
		if (!capText.isEmpty())
		{
			QColor capInk = dark ? QColor(0xB8, 0xC2, 0xCC) : QColor(0x5A, 0x50, 0x38);
			if (prelit || latched)
				capInk = dark ? QColor(0xE6, 0xEC, 0xF2) : QColor(0x2A, 0x24, 0x14);
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
		if (CopyRoutingAdapter::isVirtualChannel(out) && hoveredRow == r)
		{
			const QRect xr(labelRect.right() - 12, labelRect.center().y() + 8, 14, 14);
			p.setPen(a8(col, 230));
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
				p.setBrush(a8(ink, 70));
				p.drawEllipse(lampCenter, 5.5, 5.5);
				p.setBrush(a8(ink, 235));
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
		p.setPen(a8(QColor(t.mutedText), 220));
		const QString engrave = QStringLiteral("CHANNELS");
		p.drawText(QRect(x + 52, strip.top(), fm.horizontalAdvance(engrave) + 8, 30),
			Qt::AlignVCenter | Qt::AlignLeft, engrave);
		x += 52 + fm.horizontalAdvance(engrave) + 24;
	}

	addRect = QRect(x, strip.top(), 44, 30);
	drawCap(addRect, false, QStringLiteral("ADD"), hoveredControl == 2);
}

void HardwarePatchbayView::mousePressEvent(QMouseEvent* event)
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

void HardwarePatchbayView::mouseMoveEvent(QMouseEvent* event)
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

void HardwarePatchbayView::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
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

void HardwarePatchbayView::openChannelEditor()
{
	if (channelEditor == nullptr)
	{
		channelEditor = new QLineEdit(this);
		channelEditor->setObjectName(QStringLiteral("PatchbayChannelEditor"));
		channelEditor->setAlignment(Qt::AlignCenter);
		connect(channelEditor, &QLineEdit::editingFinished, this, &HardwarePatchbayView::commitChannelEditor);
	}
	channelEditor->setGeometry(addRect.isNull()
		? QRect(6, stripRect().top() + 4, 110, 24)
		: QRect(addRect.right() + 10, addRect.top() + 3, 110, 24));
	channelEditor->setText(QString());
	channelEditor->show();
	channelEditor->setFocus();
}

void HardwarePatchbayView::commitChannelEditor()
{
	if (channelEditor == nullptr || !channelEditor->isVisible())
		return;

	const QString name = channelEditor->text().trimmed();
	channelEditor->hide();
	if (!RoutingFold::isValidChannelName(name))
		return;

	// An existing channel just gets pinned onto the faceplate; a new name
	// becomes a virtual channel label. No routingChanged: a fresh target has
	// no sum yet and the serializer skips empty targets.
	CopyRoutingAdapter::ensureTargetChannel(workingAssignments, pinnedChannels, name);
	rebuildMatrix();
}

RoutingView* HardwarePatchbayRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent)
{
	return new HardwarePatchbayView(assignments, channelNames, portModel, parent);
}
