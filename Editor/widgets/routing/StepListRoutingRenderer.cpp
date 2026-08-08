/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StepListRoutingRenderer.h"
#include "Editor/skins/SkinPaint.h"

#include <cmath>

#include <QMenu>
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"
#include "CopyRoutingAdapter.h"

using std::vector;

StepListView::StepListView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent)
	: RoutingView(parent),
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
}

static QFont monoFont()
{
	QFont f(SkinManager::instance()->tokens().monoFontFamily);
	f.setPixelSize(12);
	return f;
}

QSize StepListView::sizeHint() const
{
	QFontMetrics fm(monoFont());
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
	const SkinTokens& t = SkinManager::instance()->tokens();
	QPainter p(this);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	const QFont mono = monoFont();
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

	// White ink vanishes on the light members of the channel palette - the
	// slate fallback every non-channel path id wears (the subwoofer dialog's
	// output matrix printed white path names on that whitish slate), and the
	// amber/cyan family reads barely better. Pick the ink per fill: white
	// only where its WCAG contrast genuinely holds, near-black otherwise.
	auto pillInk = [](const QColor& fill) -> QColor {
		auto lin = [](int v) {
			const double c = v / 255.0;
			return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
		};
		const double lum = 0.2126 * lin(fill.red())
			+ 0.7152 * lin(fill.green()) + 0.0722 * lin(fill.blue());
		return 1.05 / (lum + 0.05) >= 3.0
			? QColor(Qt::white) : QColor(QStringLiteral("#111827"));
	};

	auto drawChannelPill = [&](const QString& ch, int x, int y, int h, bool sourceSide) -> int {
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		// Fixed sources (IR file channels) are ports, not virtual channels, so
		// they keep the solid pill styling.
		const bool virt = (sourceSide && portModel.fixedSourceMode()) ? false : CopyRoutingAdapter::isVirtualChannel(ch);
		const int w = fm.horizontalAdvance(ch) + 12;
		const QRect pill(x, y + (rowH - h) / 2, w, h);
		if (virt)
		{
			p.setPen(QPen(withAlpha(col, 180), 1, Qt::DashLine));
			p.setBrush(withAlpha(col, 28));
		}
		else
		{
			p.setPen(Qt::NoPen);
			p.setBrush(col);
		}
		p.drawRect(pill);
		p.setPen(virt ? col : pillInk(col));
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

void StepListView::showAddMenu(int row, const QPoint& globalPos)
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

	if (!portModel.allowFactors)
	{
		// Without factors the only source edit is removal.
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
		editor->setObjectName(QStringLiteral("StepFactorEditor"));
		editor->setAlignment(Qt::AlignCenter);
		connect(editor, &QLineEdit::editingFinished, this, &StepListView::commitEditor);
	}
	for (const Hit& h : hits)
		if (h.row == row && h.summand == summand)
			editor->setGeometry(h.rect.adjusted(-2, -2, 2, 2));
	editor->setText(textValue);
	editor->show();
	editor->setFocus();
	editor->selectAll();
}

void StepListView::commitEditor()
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
		// Clearing the factor removes the source from the sum, mirroring the
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
	channelEditor->setGeometry(QRect(44, py + 3, 140, rowH - 6));
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
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel, QWidget* parent)
{
	return new StepListView(assignments, channelNames, portModel, parent);
}
