/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "BlockChipRoutingRenderer.h"

#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"
#include "Editor/widgets/routing/CopyRoutingAdapter.h"

using std::vector;

BlockChipView::BlockChipView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: RoutingView(parent), skinTokens(tokens),
	// Seed every device channel as an equation block so an emptied Copy can be
	// refilled from the GUI; blocks whose source sum stays empty are skipped by
	// the serializer and never reach the config line. The fold decides which
	// seeded blocks are actually shown.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced keep their block for the whole session,
	// even if their last source chip is removed.
	pinnedChannels(RoutingFold::referencedTargets(assignments))
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	refold();
}

std::vector<Assignment> BlockChipView::assignments() const
{
	return workingAssignments;
}

void BlockChipView::refold()
{
	fold = RoutingFold::fold(workingAssignments, deviceChannels, pinnedChannels,
		channelsExpanded, portModel.fixedSources);
	syncSizeToHint();
	update();
}

void BlockChipView::galleryShowcase(const QString& state)
{
	if (state == QLatin1String("expanded"))
	{
		channelsExpanded = true;
		refold();
	}
	else if (state == QLatin1String("addChannel"))
	{
		openChannelEditor();
		if (channelEditor != nullptr)
			channelEditor->setText(QStringLiteral("VS"));
	}
}

static QFont uiFont(const SkinTokens& tokens, int px)
{
	QFont f(tokens.fontFamily);
	f.setPixelSize(px);
	return f;
}

QSize BlockChipView::sizeHint() const
{
	QFontMetrics fm(uiFont(skinTokens, 13));
	int maxW = 240;
	for (int row : fold.visibleRows)
	{
		const Assignment& a = workingAssignments[row];
		int w = 24 + fm.horizontalAdvance(QString::fromStdWString(a.targetChannel)) + 24 + 24 + 44 + 30; // + add chip + × target
		for (const Assignment::Summand& s : a.sourceSum)
			w += fm.horizontalAdvance(QString::fromStdWString(s.channel)) + 90;
		maxW = qMax(maxW, w);
	}
	const int n = fold.visibleRows.size();
	int h = n * blockH + (n + 1) * gap;
	h += controlH + gap;
	return QSize(maxW + 24, h);
}

QSize BlockChipView::minimumSizeHint() const
{
	return sizeHint();
}

static QColor alpha(const QColor& c, int a) { QColor r = c; r.setAlpha(a); return r; }

void BlockChipView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const int radius = qMax(8, t.borderRadius);

	const QColor text(t.text), muted(t.mutedText), card(t.cardHover), border(t.border);
	const QColor ok(t.success), warn(t.warning), accent(t.accent);

	hits.clear();
	addHits.clear();
	removeHits.clear();
	revealRect = QRect();
	addChannelRect = QRect();

	for (int dr = 0; dr < fold.visibleRows.size(); ++dr)
	{
		const int r = fold.visibleRows[dr];
		const Assignment& a = workingAssignments[r];
		const int y = gap + dr * (blockH + gap);
		const QRect block(4, y, width() - 8, blockH);

		// Soft block background with a subtle accent edge.
		QPainterPath path;
		path.addRoundedRect(block, radius, radius);
		p.fillPath(path, card);
		p.setPen(QPen(alpha(accent, 70), 1.5));
		p.drawPath(path);

		const QString dest = QString::fromStdWString(a.targetChannel);
		const QColor destCol(CopyRoutingAdapter::channelColor(dest));
		p.setPen(Qt::NoPen);
		p.setBrush(destCol);
		p.drawRoundedRect(QRect(block.left() + 6, y, 5, blockH), 2, 2);

		QFont big = uiFont(skinTokens, 14);
		big.setBold(true);
		p.setFont(big);
		QFontMetrics bfm(big);
		int x = block.left() + 18;
		p.setPen(destCol);
		p.drawText(QRect(x, y, bfm.horizontalAdvance(dest) + 4, blockH), Qt::AlignVCenter | Qt::AlignLeft, dest);
		x += bfm.horizontalAdvance(dest) + 10;
		p.setPen(muted);
		p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, QStringLiteral("="));
		x += 22;

		QFont chipFont = uiFont(skinTokens, 13);
		p.setFont(chipFont);
		QFontMetrics fm(chipFont);

		for (int si = 0; si < (int)a.sourceSum.size(); ++si)
		{
			const Assignment::Summand& s = a.sourceSum[si];
			const QString ch = QString::fromStdWString(s.channel);
			const bool neg = s.factor < 0;
			const bool showGain = s.factor != 1.0 || s.isDecibel;

			if (si > 0)
			{
				p.setPen(neg ? warn : muted);
				p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, neg ? QStringLiteral("−") : QStringLiteral("+"));
				x += 18;
			}
			else if (neg)
			{
				p.setPen(warn);
				p.drawText(QRect(x, y, 12, blockH), Qt::AlignCenter, QStringLiteral("−"));
				x += 14;
			}

			QString factorText;
			if (showGain && portModel.allowFactors)
			{
				if (s.factor == -1.0 && !s.isDecibel)
					factorText = QStringLiteral("INV·");
				else
				{
					const double mag = neg ? -s.factor : s.factor;
					factorText = s.isDecibel ? QStringLiteral("%1dB·").arg(s.factor) : QStringLiteral("%1·").arg(mag);
				}
			}

			// Soft chip: factor·channel inside one rounded pill. Fixed sources
			// (IR file channels) are ports, not virtual channels, so they keep
			// the solid chip styling.
			const QColor col(CopyRoutingAdapter::channelColor(ch));
			const bool virt = !portModel.fixedSourceMode() && CopyRoutingAdapter::isVirtualChannel(ch);
			const int fw = fm.horizontalAdvance(factorText);
			const int cw = fm.horizontalAdvance(ch);
			const int chipW = fw + cw + 18;
			const QRect chip(x, y + (blockH - 26) / 2, chipW, 26);
			p.setPen(virt ? QPen(alpha(col, 180), 1, Qt::DashLine) : QPen(alpha(col, 120), 1));
			p.setBrush(alpha(col, virt ? 22 : 40));
			p.drawRoundedRect(chip, 13, 13);

			int cx = chip.left() + 9;
			if (!factorText.isEmpty())
			{
				p.setPen(text);
				p.drawText(QRect(cx, chip.top(), fw, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, factorText);
				// factor portion is the editable hit target
				hits.append({ r, si, QRect(cx, chip.top(), fw, chip.height()) });
				cx += fw;
			}
			else
			{
				// no factor shown: allow editing by clicking the chip
				hits.append({ r, si, chip });
			}
			p.setPen(col.darker(virt ? 100 : 130));
			QFont chBold = chipFont;
			chBold.setBold(true);
			p.setFont(chBold);
			p.drawText(QRect(cx, chip.top(), cw + 4, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, ch);
			p.setFont(chipFont);

			x += chipW + 8;
		}

		// Soft [+] chip per block: adds a source channel to this equation. This
		// is what makes an emptied Copy refillable from the GUI.
		const QRect addChip(x, y + (blockH - 26) / 2, 32, 26);
		p.setPen(QPen(alpha(accent, 140), 1, Qt::DashLine));
		p.setBrush(alpha(accent, 24));
		p.drawRoundedRect(addChip, 13, 13);
		p.setPen(alpha(accent, 220));
		p.drawText(addChip, Qt::AlignCenter, QStringLiteral("+"));
		addHits.append({ r, addChip });
		x += 40;

		// A virtual channel's block can be removed: hovering the block shows a
		// quiet × pill at its tail (device channels fold instead of leaving,
		// so they never get one). Muted, small, never alarming.
		if (CopyRoutingAdapter::isVirtualChannel(dest) && hoveredRow == r)
		{
			const QRect xChip(x, y + (blockH - 22) / 2, 22, 22);
			p.setPen(QPen(alpha(muted, 140), 1));
			p.setBrush(alpha(muted, 26));
			p.drawEllipse(xChip);
			p.setPen(alpha(muted, 230));
			p.drawText(xChip, Qt::AlignCenter, QStringLiteral("×"));
			removeHits.append({ r, xChip });
		}
	}

	// The control row: a quiet "show more channels" pill (OFF-state pill
	// grammar - sunken ground, 1px border, muted ink; hover raises it one
	// step) and the dashed "add channel" chip (the not-hardware-backed
	// grammar shared with the per-block [+]).
	const int y = gap + fold.visibleRows.size() * (blockH + gap);
	QFont chipFont = uiFont(skinTokens, 12);
	p.setFont(chipFont);
	QFontMetrics fm(chipFont);
	int x = 8;

	if (fold.hiddenChannels > 0 || channelsExpanded)
	{
		const QString caption = channelsExpanded
			? tr("Show fewer channels")
			: tr("Show %n more channel(s)", nullptr, fold.hiddenChannels);
		const int w = fm.horizontalAdvance(caption) + 24;
		revealRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 1;
		p.setPen(QPen(alpha(border, 160), 1));
		p.setBrush(hovered ? alpha(border, 60) : alpha(border, 30));
		p.drawRoundedRect(revealRect, (controlH - 4) / 2, (controlH - 4) / 2);
		p.setPen(hovered ? text : muted);
		p.drawText(revealRect, Qt::AlignCenter, caption);
		x += w + 10;
	}

	{
		const QString caption = QStringLiteral("+ ") + tr("Add channel");
		const int w = fm.horizontalAdvance(caption) + 24;
		addChannelRect = QRect(x, y, w, controlH - 4);
		const bool hovered = hoveredControl == 2;
		p.setPen(QPen(alpha(accent, hovered ? 220 : 140), 1, Qt::DashLine));
		p.setBrush(alpha(accent, hovered ? 40 : 24));
		p.drawRoundedRect(addChannelRect, (controlH - 4) / 2, (controlH - 4) / 2);
		p.setPen(alpha(accent, hovered ? 255 : 220));
		p.drawText(addChannelRect, Qt::AlignCenter, caption);
	}
}

void BlockChipView::mousePressEvent(QMouseEvent* event)
{
	for (const AddHit& h : removeHits)
	{
		if (h.rect.contains(event->pos()))
		{
			const QString channel = QString::fromStdWString(workingAssignments[h.row].targetChannel);
			for (int i = pinnedChannels.size() - 1; i >= 0; i--)
				if (pinnedChannels[i].compare(channel, Qt::CaseInsensitive) == 0)
					pinnedChannels.removeAt(i);
			const bool changed = RoutingFold::removeChannel(workingAssignments, channel);
			refold();
			if (changed)
				emit routingChanged();
			return;
		}
	}
	for (const AddHit& h : addHits)
	{
		if (h.rect.contains(event->pos()))
		{
			showAddMenu(h.row, mapToGlobal(h.rect.bottomLeft()));
			return;
		}
	}
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		refold();
		return;
	}
	if (!addChannelRect.isNull() && addChannelRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}
	RoutingView::mousePressEvent(event);
}

void BlockChipView::mouseMoveEvent(QMouseEvent* event)
{
	int control = 0;
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
		control = 1;
	else if (!addChannelRect.isNull() && addChannelRect.contains(event->pos()))
		control = 2;

	int row = -1;
	const int slot = (event->pos().y() - gap) / (blockH + gap);
	if (event->pos().y() >= gap && slot >= 0 && slot < fold.visibleRows.size())
		row = fold.visibleRows[slot];

	if (control != hoveredControl || row != hoveredRow)
	{
		hoveredControl = control;
		hoveredRow = row;
		setCursor(control != 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
	RoutingView::mouseMoveEvent(event);
}

void BlockChipView::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
}

void BlockChipView::showAddMenu(int row, const QPoint& globalPos)
{
	if (row < 0 || row >= (int)workingAssignments.size())
		return;

	auto inSum = [this, row](const QString& channel) {
		for (const Assignment::Summand& s : workingAssignments[row].sourceSum)
			if (QString::fromStdWString(s.channel).compare(channel, Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};

	QStringList candidates;
	auto addCandidate = [&](const QString& channel) {
		if (channel.isEmpty() || channel == QLatin1String(" ")
			|| inSum(channel) || candidates.contains(channel, Qt::CaseInsensitive))
			return;
		candidates.append(channel);
	};
	if (portModel.fixedSourceMode())
	{
		// Fixed sources (IR file channels): the port list is the whole menu.
		for (const QString& port : portModel.fixedSources)
			addCandidate(port);
	}
	else
	{
		for (const std::wstring& name : deviceChannels)
			addCandidate(QString::fromStdWString(name));
		// Channels the command references elsewhere (e.g. virtual channels) stay
		// available even when the device layout is unknown.
		for (const Assignment& other : workingAssignments)
		{
			addCandidate(QString::fromStdWString(other.targetChannel));
			for (const Assignment::Summand& s : other.sourceSum)
				addCandidate(QString::fromStdWString(s.channel));
		}
	}
	if (candidates.isEmpty())
		return;

	QMenu menu(this);
	for (const QString& channel : candidates)
		menu.addAction(channel);
	const QAction* chosen = menu.exec(globalPos);
	if (chosen == nullptr)
		return;

	Assignment::Summand s;
	s.factor = 1.0;
	s.isDecibel = false;
	s.channel = chosen->text().toStdWString();
	workingAssignments[row].sourceSum.push_back(s);
	refold();
	emit routingChanged();
}

void BlockChipView::mouseDoubleClickEvent(QMouseEvent* event)
{
	int row = -1, summand = -1;
	for (const Hit& h : hits)
		if (h.rect.contains(event->pos())) { row = h.row; summand = h.summand; break; }
	if (row < 0)
		return;

	if (!portModel.allowFactors)
	{
		// Without factors the only chip edit is removal.
		Assignment& a = workingAssignments[row];
		if (summand >= 0 && summand < (int)a.sourceSum.size())
		{
			a.sourceSum.erase(a.sourceSum.begin() + summand);
			refold();
			emit routingChanged();
		}
		return;
	}

	commitEditor();
	editRow = row;
	editSummand = summand;

	const Assignment::Summand& s = workingAssignments[row].sourceSum[summand];
	const QString textValue = s.isDecibel ? QStringLiteral("%1dB").arg(s.factor) : QString::number(s.factor);

	if (editor == nullptr)
	{
		editor = new QLineEdit(this);
		editor->setObjectName(QStringLiteral("BlockFactorEditor"));
		editor->setAlignment(Qt::AlignCenter);
		connect(editor, &QLineEdit::editingFinished, this, &BlockChipView::commitEditor);
	}
	for (const Hit& h : hits)
		if (h.row == row && h.summand == summand)
			editor->setGeometry(h.rect.adjusted(-3, 0, 28, 0));
	editor->setText(textValue);
	editor->show();
	editor->setFocus();
	editor->selectAll();
}

void BlockChipView::commitEditor()
{
	if (editor == nullptr || !editor->isVisible() || editRow < 0)
		return;

	const int row = editRow, si = editSummand;
	editRow = editSummand = -1;
	QString raw = editor->text().trimmed();
	editor->hide();

	if (row >= (int)workingAssignments.size() || si >= (int)workingAssignments[row].sourceSum.size())
		return;

	if (raw.isEmpty())
	{
		// Clearing the factor removes the source chip, mirroring the
		// crosspoint / patch-bay grids.
		Assignment& a = workingAssignments[row];
		a.sourceSum.erase(a.sourceSum.begin() + si);
		refold();
		emit routingChanged();
		return;
	}

	Assignment::Summand& s = workingAssignments[row].sourceSum[si];
	CopyRoutingAdapter::parseFactorToken(raw, s);
	refold();
	emit routingChanged();
}

void BlockChipView::openChannelEditor()
{
	if (channelEditor == nullptr)
	{
		channelEditor = new QLineEdit(this);
		channelEditor->setObjectName(QStringLiteral("BlockChannelEditor"));
		connect(channelEditor, &QLineEdit::editingFinished, this, &BlockChipView::commitChannelEditor);
	}
	const QRect anchor = addChannelRect.isNull()
		? QRect(8, gap + fold.visibleRows.size() * (blockH + gap), 120, controlH - 4)
		: addChannelRect;
	channelEditor->setGeometry(anchor.adjusted(0, 0, 40, 0));
	channelEditor->setText(QString());
	channelEditor->show();
	channelEditor->setFocus();
}

void BlockChipView::commitChannelEditor()
{
	if (channelEditor == nullptr || !channelEditor->isVisible())
		return;

	const QString name = channelEditor->text().trimmed();
	channelEditor->hide();
	if (!RoutingFold::isValidChannelName(name))
		return;

	// An existing channel just gets its block back; a new name becomes a
	// virtual channel block. No routingChanged: a fresh target has no sum yet
	// and the serializer skips empty targets.
	CopyRoutingAdapter::ensureTargetChannel(workingAssignments, pinnedChannels, name);
	refold();
}

RoutingView* BlockChipRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new BlockChipView(assignments, channelNames, portModel, parent, tokens);
}
