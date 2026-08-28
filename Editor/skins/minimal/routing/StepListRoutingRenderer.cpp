/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StepListRoutingRenderer.h"
#include "Editor/skins/shared/SkinPaint.h"

#include <QCompleter>
#include <QMenu>
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QStringListModel>

#include "Editor/SkinManager.h"
#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/skins/minimal/MinimalChannelInk.h"

using std::vector;

StepListView::StepListView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens)
	: RoutingView(parent), skinTokens(tokens),
	// Seed every device channel as a step so an emptied Copy can be refilled
	// from the GUI; steps whose source sum stays empty are skipped by the
	// serializer and never reach the config line. The fold decides which
	// seeded steps are actually listed.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced stay listed for the whole session, even
	// if their last source is removed.
	pinnedChannels(RoutingFold::referencedTargets(assignments))
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
	refold();
}

std::vector<Assignment> StepListView::assignments() const
{
	return workingAssignments;
}

void StepListView::refold()
{
	fold = RoutingFold::fold(workingAssignments, deviceChannels, pinnedChannels,
		channelsExpanded, portModel.fixedSources);
	syncSizeToHint();
	update();
}

void StepListView::galleryShowcase(const QString& state)
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
	else if (state.startsWith(QLatin1String("editSource:")))
	{
		// The gate's stand-in for a double-click: the named target's first
		// summand. paintEvent lays out the hit-rects the editor sits on, so
		// the view must have painted once (the caller shows it first).
		const int row = rowIndexOf(state.mid(11));
		if (row >= 0 && !workingAssignments[row].sourceSum.empty())
			openSourceEditor(row, 0);
	}
}

int StepListView::rowIndexOf(const QString& target) const
{
	for (int i = 0; i < (int)workingAssignments.size(); i++)
		if (QString::fromStdWString(workingAssignments[i].targetChannel).compare(target, Qt::CaseInsensitive) == 0)
			return i;
	return -1;
}

QStringList StepListView::sourceCandidates(const QString& target) const
{
	return sourceCandidatesForRow(rowIndexOf(target));
}

bool StepListView::connectSource(const QString& target, const QString& source)
{
	return addSourceToRow(rowIndexOf(target), source);
}

static QFont monoFont(const SkinTokens& tokens)
{
	QFont f(tokens.monoFontFamily);
	f.setPixelSize(12);
	return f;
}

QSize StepListView::sizeHint() const
{
	QFontMetrics fm(monoFont(skinTokens));
	int maxWidth = 220;
	for (int row : fold.visibleRows)
	{
		const Assignment& a = workingAssignments[row];
		int w = 36 + 52 + 28 + 26 + 26; // number + dest + arrow + [+] target + [x] target
		for (const Assignment::Summand& s : a.sourceSum)
		{
			const QString ch = QString::fromStdWString(s.channel);
			w += 18 + fm.horizontalAdvance(ch) + 18 + fm.horizontalAdvance(QStringLiteral("x-0.000")) + 18;
		}
		maxWidth = qMax(maxWidth, w);
	}
	// The fold line (when channels are folded away) and the prompt/cursor
	// line that closes the listing.
	int extraLines = 1;
	if (fold.hiddenChannels > 0 || channelsExpanded)
		extraLines++;
	return QSize(maxWidth + 16, headerH + (fold.visibleRows.size() + extraLines) * rowH + 8);
}

QSize StepListView::minimumSizeHint() const
{
	return sizeHint();
}

// withAlpha lives in the shared SkinPaint.h.

void StepListView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = skinTokens;
	QPainter p(this);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	const QFont mono = monoFont(skinTokens);
	p.setFont(mono);
	QFontMetrics fm(mono);

	const QColor text(t.text), muted(t.mutedText), border(t.border);
	const QColor ok(t.success), warn(t.warning);

	hits.clear();
	addHits.clear();
	removeHits.clear();
	foldRect = QRect();
	promptRect = QRect();

	// Header
	p.setPen(muted);
	p.drawText(QRect(0, 0, 36, headerH), Qt::AlignCenter, QStringLiteral("#"));
	p.drawText(QRect(40, 0, 52, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DEST"));
	p.drawText(QRect(120, 0, 200, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("SOURCES"));
	p.setPen(QPen(withAlpha(border, 160), 1));
	p.drawLine(0, headerH, width(), headerH);

	const bool foldLine = fold.hiddenChannels > 0 || channelsExpanded;
	const int listedRows = fold.visibleRows.size() + (foldLine ? 1 : 0);

	// Line-number gutter: the number column is set off by a hairline, like a
	// terminal listing's gutter. Runs through the prompt line so the closing
	// cursor sits inside the same frame.
	const int listingBottom = headerH + (listedRows + 1) * rowH;
	p.setPen(QPen(withAlpha(border, 120), 1));
	p.drawLine(36, headerH, 36, listingBottom);

	auto drawChannelPill = [&](const QString& ch, int x, int y, int h, bool sourceSide) -> int {
		// A bare colored token, the way a terminal marks special text (ls
		// --color). Only virtual channels keep the dashed hairline frame:
		// fixed sources (IR file channels) are ports, not virtual channels.
		const bool virt = (sourceSide && portModel.fixedSourceMode()) ? false : CopyRoutingAdapter::isVirtualChannel(ch);
		const QColor ink = minimalChannelInk(QColor(CopyRoutingAdapter::channelColor(ch)), t.dark);
		const int w = fm.horizontalAdvance(ch) + 12;
		const QRect pill(x, y + (rowH - h) / 2, w, h);
		if (virt)
		{
			p.setPen(QPen(withAlpha(ink, 150), 1, Qt::DashLine));
			p.setBrush(Qt::NoBrush);
			p.drawRect(pill);
		}
		p.setPen(ink);
		p.drawText(pill, Qt::AlignCenter, ch);
		return w;
	};

	// A bracket target in the [+] grammar: 1px hairline box, muted glyph,
	// hover = exactly one background step.
	auto drawBracketTarget = [&](const QRect& rect, const QString& glyph, bool hovered) {
		p.setPen(QPen(withAlpha(border, 160), 1));
		p.setBrush(withAlpha(border, hovered ? 52 : 26));
		p.drawRect(rect);
		p.setPen(hovered ? text : muted);
		p.drawText(rect, Qt::AlignCenter, glyph);
	};

	for (int dr = 0; dr < fold.visibleRows.size(); ++dr)
	{
		const int r = fold.visibleRows[dr];
		const Assignment& a = workingAssignments[r];
		const int y = headerH + dr * rowH;
		if (dr % 2 == 1)
			p.fillRect(QRect(0, y, width(), rowH), withAlpha(border, 22));

		// Step number: a zero-padded page coordinate, the picker's console
		// numbering law applied to the listing.
		p.setPen(muted);
		p.drawText(QRect(0, y, 36, rowH), Qt::AlignCenter,
			QStringLiteral("%1").arg(dr + 1, 2, 10, QLatin1Char('0')));

		// Destination
		int x = 40;
		const QString dest = QString::fromStdWString(a.targetChannel);
		x += drawChannelPill(dest, x, y, 20, false) + 8;

		// Arrow
		p.setPen(muted);
		p.drawText(QRect(x, y, 20, rowH), Qt::AlignCenter, QStringLiteral("←"));
		x += 24;

		// Sources
		for (int si = 0; si < (int)a.sourceSum.size(); ++si)
		{
			const Assignment::Summand& s = a.sourceSum[si];
			const QString ch = QString::fromStdWString(s.channel);
			const bool neg = s.factor < 0;
			const bool showGain = s.factor != 1.0 || s.isDecibel;

			if (si > 0 || neg)
			{
				p.setPen(neg ? warn : ok);
				p.drawText(QRect(x, y, 12, rowH), Qt::AlignCenter, neg ? QStringLiteral("−") : QStringLiteral("+"));
				x += 14;
			}

			const int pillW = drawChannelPill(ch, x, y, 18, true);
			// The pill itself is an edit target so unity summands (which show no
			// gain label) can still be edited or removed via the factor editor.
			hits.append({ r, si, QRect(x, y + (rowH - 18) / 2, pillW, 18) });
			x += pillW + 4;

			if (showGain && portModel.allowFactors)
			{
				QString label;
				if (s.factor == -1.0 && !s.isDecibel)
					label = QStringLiteral("INV");
				else
				{
					const double mag = neg ? -s.factor : s.factor;
					label = s.isDecibel ? QStringLiteral("%1dB").arg(s.factor) : QStringLiteral("×%1").arg(mag);
				}
				const int w = fm.horizontalAdvance(label) + 8;
				const QRect gr(x, y + (rowH - 18) / 2, w, 18);
				p.setPen(QPen(withAlpha(border, 160), 1));
				p.setBrush(withAlpha(QColor(t.accent), 26));
				p.drawRect(gr);
				p.setPen(text);
				p.drawText(gr, Qt::AlignCenter, label);
				hits.append({ r, si, gr });
				x += w + 10;
			}
			else
			{
				x += 10;
			}

			// An open source editor is wider than the summand it replaces:
			// the rest of the step flows around it instead of vanishing
			// underneath.
			if (sourceEditor != nullptr && sourceEditor->isVisible() && editRow == r && editSummand == si)
				x = qMax(x, sourceEditor->geometry().right() + 8);
		}

		// Bracketed [+] target per step: adds a source channel to this sum. This
		// is what makes an emptied Copy refillable from the GUI.
		const QRect addRect(x, y + (rowH - 18) / 2, 18, 18);
		drawBracketTarget(addRect, QStringLiteral("+"), false);
		addHits.append({ r, addRect });
		x += 22;

		// A virtual channel can leave the listing: hovering its step exposes
		// an [x] bracket target (device channels fold instead of leaving).
		if (CopyRoutingAdapter::isVirtualChannel(dest) && hoveredRow == r)
		{
			const QRect xRect(x, y + (rowH - 18) / 2, 18, 18);
			drawBracketTarget(xRect, QStringLiteral("x"), false);
			removeHits.append({ r, xRect });
		}
	}

	// The fold line: the pager's ellipsis row. The bracket target reveals the
	// folded device channels ([+N CH]) or folds the listing back ([FOLD]).
	int py = headerH + fold.visibleRows.size() * rowH;
	if (foldLine)
	{
		p.setPen(muted);
		p.drawText(QRect(0, py, 36, rowH), Qt::AlignCenter, QStringLiteral("··"));
		const QString caption = channelsExpanded
			? QStringLiteral("[FOLD]")
			: QStringLiteral("[+%1 CH]").arg(fold.hiddenChannels);
		const int w = fm.horizontalAdvance(caption) + 8;
		foldRect = QRect(44, py + (rowH - 18) / 2, w, 18);
		p.setPen(QPen(withAlpha(border, 160), 1));
		p.setBrush(withAlpha(border, hoveredControl == 1 ? 52 : 26));
		p.drawRect(foldRect);
		p.setPen(hoveredControl == 1 ? text : muted);
		p.drawText(foldRect, Qt::AlignCenter, caption);
		py += rowH;
	}

	// The session line: a prompt and a steady block cursor after the last
	// step. In Copy mode the prompt is the add-channel entry - click it and
	// type a new (virtual) channel name; in fixed-source mode it stays pure
	// staging.
	p.setPen(muted);
	p.drawText(QRect(0, py, 36, rowH), Qt::AlignCenter, QStringLiteral(">"));
	p.fillRect(QRect(44, py + (rowH - 15) / 2, 8, 15), withAlpha(QColor(t.accent), 210));
	promptRect = QRect(0, py, width(), rowH);
	if (hoveredControl == 2 && (channelEditor == nullptr || !channelEditor->isVisible()))
	{
		p.setPen(withAlpha(muted, 190));
		p.drawText(QRect(58, py, 220, rowH), Qt::AlignLeft | Qt::AlignVCenter,
			QStringLiteral("add channel"));
	}
}

void StepListView::mousePressEvent(QMouseEvent* event)
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
	if (!foldRect.isNull() && foldRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		refold();
		return;
	}
	if (!promptRect.isNull() && promptRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}
	RoutingView::mousePressEvent(event);
}

void StepListView::mouseMoveEvent(QMouseEvent* event)
{
	int control = 0;
	if (!foldRect.isNull() && foldRect.contains(event->pos()))
		control = 1;
	else if (!promptRect.isNull() && promptRect.contains(event->pos()))
		control = 2;

	int row = -1;
	const int dr = (event->pos().y() - headerH) / rowH;
	if (event->pos().y() >= headerH && dr >= 0 && dr < fold.visibleRows.size())
		row = fold.visibleRows[dr];

	if (control != hoveredControl || row != hoveredRow)
	{
		hoveredControl = control;
		hoveredRow = row;
		setCursor(control != 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
	RoutingView::mouseMoveEvent(event);
}

void StepListView::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
}

QStringList StepListView::sourceCandidatesForRow(int row) const
{
	QStringList candidates;
	if (row < 0 || row >= (int)workingAssignments.size())
		return candidates;

	auto inSum = [this, row](const QString& channel) {
		for (const Assignment::Summand& s : workingAssignments[row].sourceSum)
			if (QString::fromStdWString(s.channel).compare(channel, Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};
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
	return candidates;
}

bool StepListView::addSourceToRow(int row, const QString& channel)
{
	if (row < 0 || row >= (int)workingAssignments.size() || channel.isEmpty())
		return false;
	for (const Assignment::Summand& s : workingAssignments[row].sourceSum)
		if (QString::fromStdWString(s.channel).compare(channel, Qt::CaseInsensitive) == 0)
			return false;

	Assignment::Summand s;
	s.factor = 1.0;
	s.isDecibel = false;
	s.channel = channel.toStdWString();
	workingAssignments[row].sourceSum.push_back(s);
	refold();
	emit routingChanged();
	return true;
}

void StepListView::showAddMenu(int row, const QPoint& globalPos)
{
	const QStringList candidates = sourceCandidatesForRow(row);
	if (candidates.isEmpty())
		return;

	QMenu menu(this);
	for (const QString& channel : candidates)
		menu.addAction(channel);
	const QAction* chosen = menu.exec(globalPos);
	if (chosen == nullptr)
		return;
	addSourceToRow(row, chosen->text());
}

void StepListView::mouseDoubleClickEvent(QMouseEvent* event)
{
	int row = -1, summand = -1;
	for (const Hit& h : hits)
	{
		if (h.rect.contains(event->pos()))
		{
			row = h.row;
			summand = h.summand;
			break;
		}
	}
	if (row < 0)
		return;
	openSourceEditor(row, summand);
}

void StepListView::openSourceEditor(int row, int summand)
{
	if (row < 0 || row >= (int)workingAssignments.size()
		|| summand < 0 || summand >= (int)workingAssignments[row].sourceSum.size())
		return;

	commitSourceEditor();
	editRow = row;
	editSummand = summand;

	if (sourceEditor == nullptr)
	{
		sourceEditor = new QLineEdit(this);
		sourceEditor->setObjectName(QStringLiteral("StepSourceEditor"));
		// Channel-name completion from the step's own candidate list: the
		// terminal's tab-completion, so the hint the [+] menu carries is at
		// hand while typing too.
		sourceCompletions = new QStringListModel(sourceEditor);
		sourceCompleter = new QCompleter(sourceCompletions, sourceEditor);
		sourceCompleter->setCaseSensitivity(Qt::CaseInsensitive);
		sourceCompleter->setCompletionMode(QCompleter::PopupCompletion);
		sourceEditor->setCompleter(sourceCompleter);
		connect(sourceEditor, &QLineEdit::editingFinished, this, &StepListView::commitSourceEditor);
	}

	// The editor spans the summand's token and gain label, and never less
	// than a full "-0.000dB*LFE": the field must show what it holds. It
	// stays inside the view's width, sliding left when the step is long.
	QRect span;
	for (const Hit& h : hits)
		if (h.row == row && h.summand == summand)
			span |= h.rect;
	// The editor's own face (the sheet pins the listing's mono face on it),
	// polished first so a fresh widget does not measure with the default.
	sourceEditor->ensurePolished();
	const QFontMetrics fm = sourceEditor->fontMetrics();
	const int minimumWidth = fm.horizontalAdvance(QStringLiteral("-0.000dB*LFE")) + 24;
	const int w = qMax(span.width() + 12, minimumWidth);
	int x = span.isNull() ? 40 : span.left() - 6;
	if (x + w > width())
		x = qMax(0, width() - w);
	const int y = span.isNull() ? headerH + 2 : span.center().y() - (rowH - 4) / 2;
	sourceEditor->setGeometry(x, y, w, rowH - 4);

	QStringList completions = sourceCandidatesForRow(row);
	const Assignment::Summand& s = workingAssignments[row].sourceSum[summand];
	completions.prepend(QString::fromStdWString(s.channel));
	sourceCompletions->setStringList(completions);

	sourceEditor->setText(RoutingFold::sourceToken(s));
	sourceEditor->show();
	sourceEditor->setFocus();
	sourceEditor->selectAll();
	// Reflow the step around the editor (paintEvent).
	update();
}

void StepListView::commitSourceEditor()
{
	if (sourceEditor == nullptr || !sourceEditor->isVisible() || editRow < 0)
		return;

	const int row = editRow, si = editSummand;
	editRow = editSummand = -1;
	const QString raw = sourceEditor->text().trimmed();
	sourceEditor->hide();
	// The step flowed around the editor; flow it back even when nothing
	// changes below.
	update();

	if (row >= (int)workingAssignments.size() || si >= (int)workingAssignments[row].sourceSum.size())
		return;

	if (raw.isEmpty())
	{
		// Clearing the token removes the source from the sum, mirroring the
		// crosspoint / patch-bay grids.
		Assignment& a = workingAssignments[row];
		a.sourceSum.erase(a.sourceSum.begin() + si);
		refold();
		emit routingChanged();
		return;
	}

	Assignment::Summand& s = workingAssignments[row].sourceSum[si];
	Assignment::Summand edited = s;
	// A token the grammar cannot read leaves the step as it was; so does a
	// gain where the port model allows none.
	if (!RoutingFold::parseSourceToken(raw, portModel.fixedSourceMode(), edited))
		return;
	if (!portModel.allowFactors && (edited.factor != 1.0 || edited.isDecibel))
		return;
	if (edited.channel == s.channel && edited.factor == s.factor && edited.isDecibel == s.isDecibel)
		return;
	s = edited;
	refold();
	emit routingChanged();
}

void StepListView::openChannelEditor()
{
	if (channelEditor == nullptr)
	{
		channelEditor = new QLineEdit(this);
		channelEditor->setObjectName(QStringLiteral("StepChannelEditor"));
		connect(channelEditor, &QLineEdit::editingFinished, this, &StepListView::commitChannelEditor);
	}
	const int py = promptRect.isNull()
		? headerH + fold.visibleRows.size() * rowH
		: promptRect.top();
	channelEditor->setGeometry(QRect(44, py + 2, 160, rowH - 4));
	channelEditor->setText(QString());
	channelEditor->show();
	channelEditor->setFocus();
}

void StepListView::commitChannelEditor()
{
	if (channelEditor == nullptr || !channelEditor->isVisible())
		return;

	const QString name = channelEditor->text().trimmed();
	channelEditor->hide();
	if (!RoutingFold::isValidChannelName(name))
		return;

	// An existing channel just gets pinned back into the listing; a new name
	// becomes a virtual step. No routingChanged: a fresh target has no sum
	// yet and the serializer skips empty targets.
	CopyRoutingAdapter::ensureTargetChannel(workingAssignments, pinnedChannels, name);
	refold();
}

RoutingView* StepListRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent,
	const SkinTokens& tokens)
{
	return new StepListView(assignments, channelNames, portModel, parent, tokens);
}
