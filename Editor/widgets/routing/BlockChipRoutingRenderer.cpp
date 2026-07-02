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
#include "CopyRoutingAdapter.h"

using std::vector;

BlockChipView::BlockChipView(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, QWidget* parent)
	: RoutingView(parent),
	// Seed every device channel as an equation block so an emptied Copy can be
	// refilled from the GUI; blocks whose source sum stays empty are skipped by
	// the serializer and never reach the config line.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
}

std::vector<Assignment> BlockChipView::assignments() const
{
	return workingAssignments;
}

static QFont uiFont(int px)
{
	QFont f(SkinManager::instance()->tokens().fontFamily);
	f.setPixelSize(px);
	return f;
}

QSize BlockChipView::sizeHint() const
{
	QFontMetrics fm(uiFont(13));
	int maxW = 240;
	for (const Assignment& a : workingAssignments)
	{
		int w = 24 + fm.horizontalAdvance(QString::fromStdWString(a.targetChannel)) + 24 + 24 + 44; // + add chip
		for (const Assignment::Summand& s : a.sourceSum)
			w += fm.horizontalAdvance(QString::fromStdWString(s.channel)) + 90;
		maxW = qMax(maxW, w);
	}
	const int n = (int)workingAssignments.size();
	return QSize(maxW + 24, n * blockH + (n + 1) * gap);
}

QSize BlockChipView::minimumSizeHint() const
{
	return sizeHint();
}

static QColor alpha(const QColor& c, int a) { QColor r = c; r.setAlpha(a); return r; }

void BlockChipView::paintEvent(QPaintEvent*)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const int radius = qMax(8, t.borderRadius);

	const QColor text(t.text), muted(t.mutedText), card(t.cardHover), border(t.border);
	const QColor ok(t.success), warn(t.warning), accent(t.accent);

	hits.clear();
	addHits.clear();

	for (int r = 0; r < (int)workingAssignments.size(); ++r)
	{
		const Assignment& a = workingAssignments[r];
		const int y = gap + r * (blockH + gap);
		const QRect block(4, y, width() - 8, blockH);

		// Soft block background with a subtle accent edge.
		QPainterPath path;
		path.addRoundedRect(block, radius, radius);
		p.fillPath(path, card);
		p.setPen(QPen(alpha(accent, 70), 1.5));
		p.drawPath(path);

		const QColor destCol(CopyRoutingAdapter::channelColor(QString::fromStdWString(a.targetChannel)));
		p.setPen(Qt::NoPen);
		p.setBrush(destCol);
		p.drawRoundedRect(QRect(block.left() + 6, y, 5, blockH), 2, 2);

		QFont big = uiFont(14);
		big.setBold(true);
		p.setFont(big);
		QFontMetrics bfm(big);
		int x = block.left() + 18;
		const QString dest = QString::fromStdWString(a.targetChannel);
		p.setPen(destCol);
		p.drawText(QRect(x, y, bfm.horizontalAdvance(dest) + 4, blockH), Qt::AlignVCenter | Qt::AlignLeft, dest);
		x += bfm.horizontalAdvance(dest) + 10;
		p.setPen(muted);
		p.drawText(QRect(x, y, 16, blockH), Qt::AlignCenter, QStringLiteral("="));
		x += 22;

		QFont chipFont = uiFont(13);
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
			if (showGain)
			{
				if (s.factor == -1.0 && !s.isDecibel)
					factorText = QStringLiteral("INV·");
				else
				{
					const double mag = neg ? -s.factor : s.factor;
					factorText = s.isDecibel ? QStringLiteral("%1dB·").arg(s.factor) : QStringLiteral("%1·").arg(mag);
				}
			}

			// Soft chip: factor·channel inside one rounded pill.
			const QColor col(CopyRoutingAdapter::channelColor(ch));
			const bool virt = CopyRoutingAdapter::isVirtualChannel(ch);
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
	}
}

void BlockChipView::mousePressEvent(QMouseEvent* event)
{
	for (const AddHit& h : addHits)
	{
		if (h.rect.contains(event->pos()))
		{
			showAddMenu(h.row, mapToGlobal(h.rect.bottomLeft()));
			return;
		}
	}
	RoutingView::mousePressEvent(event);
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
	updateGeometry();
	update();
	emit routingChanged();
}

void BlockChipView::mouseDoubleClickEvent(QMouseEvent* event)
{
	int row = -1, summand = -1;
	for (const Hit& h : hits)
		if (h.rect.contains(event->pos())) { row = h.row; summand = h.summand; break; }
	if (row < 0)
		return;

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
		updateGeometry();
		update();
		emit routingChanged();
		return;
	}

	Assignment::Summand& s = workingAssignments[row].sourceSum[si];
	if (raw.compare(QLatin1String("INV"), Qt::CaseInsensitive) == 0)
	{
		s.factor = -1.0;
		s.isDecibel = false;
	}
	else
	{
		bool isDb = false;
		QString num = raw;
		if (num.right(2).toLower() == QLatin1String("db"))
		{
			isDb = true;
			num = num.left(num.size() - 2).trimmed();
		}
		bool ok = false;
		const double parsed = num.toDouble(&ok);
		if (ok)
		{
			s.factor = parsed;
			s.isDecibel = isDb;
		}
	}
	updateGeometry();
	update();
	emit routingChanged();
}

RoutingView* BlockChipRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, QWidget* parent)
{
	return new BlockChipView(assignments, channelNames, parent);
}
