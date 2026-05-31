/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StepListRoutingRenderer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

#include "Editor/SkinManager.h"
#include "CopyRoutingAdapter.h"

using std::vector;

StepListView::StepListView(const vector<Assignment>& assignments, QWidget* parent)
	: RoutingView(parent), workingAssignments(assignments)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
}

std::vector<Assignment> StepListView::assignments() const
{
	return workingAssignments;
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
	for (const Assignment& a : workingAssignments)
	{
		int w = 36 + 52 + 28; // number + dest + arrow
		for (const Assignment::Summand& s : a.sourceSum)
		{
			const QString ch = QString::fromStdWString(s.channel);
			w += 18 + fm.horizontalAdvance(ch) + 18 + fm.horizontalAdvance(QStringLiteral("x-0.000")) + 18;
		}
		maxWidth = qMax(maxWidth, w);
	}
	return QSize(maxWidth + 16, headerH + (int)workingAssignments.size() * rowH + 8);
}

QSize StepListView::minimumSizeHint() const
{
	return sizeHint();
}

static QColor withAlpha(const QColor& c, int a) { QColor r = c; r.setAlpha(a); return r; }

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

	// Header
	p.setPen(muted);
	p.drawText(QRect(0, 0, 36, headerH), Qt::AlignCenter, QStringLiteral("#"));
	p.drawText(QRect(40, 0, 52, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DEST"));
	p.drawText(QRect(120, 0, 200, headerH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("SOURCES"));
	p.setPen(QPen(withAlpha(border, 160), 1));
	p.drawLine(0, headerH, width(), headerH);

	auto drawChannelPill = [&](const QString& ch, int x, int y, int h) -> int {
		const QColor col(CopyRoutingAdapter::channelColor(ch));
		const bool virt = CopyRoutingAdapter::isVirtualChannel(ch);
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
		p.setPen(virt ? col : QColor(Qt::white));
		p.drawText(pill, Qt::AlignCenter, ch);
		return w;
	};

	for (int r = 0; r < (int)workingAssignments.size(); ++r)
	{
		const Assignment& a = workingAssignments[r];
		const int y = headerH + r * rowH;
		if (r % 2 == 1)
			p.fillRect(QRect(0, y, width(), rowH), withAlpha(border, 22));

		// Step number
		p.setPen(muted);
		p.drawText(QRect(0, y, 36, rowH), Qt::AlignCenter, QString::number(r + 1));

		// Destination
		int x = 40;
		x += drawChannelPill(QString::fromStdWString(a.targetChannel), x, y, 20) + 8;

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

			x += drawChannelPill(ch, x, y, 18) + 4;

			if (showGain)
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
	}
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

RoutingView* StepListRoutingRenderer::create(const vector<Assignment>& assignments,
	const vector<std::wstring>& /*channelNames*/, QWidget* parent)
{
	return new StepListView(assignments, parent);
}
