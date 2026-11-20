// Soft skin re-foundation mockup (EqualizerAPO-XT). Standalone: paints still
// images with QPainter. Not wired to the Editor.
//
// usage: softmock <fontDir> <outDir>

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <functional>

struct Pal
{
	bool dark;
	QColor bg, surface, sunk, text, muted, line, lineStrong, edge;
	QColor mark, markHover, markText;
	QColor success, warning, error;
};

static Pal lightPal()
{
	// Softreceiver standard skin, light: neutral grounds, ink emphasis.
	return { false,
			 "#f6f6f5", "#ffffff", "#f1f1ef", "#1c1c1b", "#5d5d59", "#e2e2df", "#c9c9c5", "#8a8a85",
			 "#1c1c1b", "#3b3b38", "#ffffff",
			 "#2e7d46", "#965a00", "#b42d14" };
}

static Pal darkPal()
{
	// Softreceiver standard skin, dark grounds; emphasis is the SECS 2R blue.
	return { true,
			 "#0f1012", "#17191c", "#131517", "#e7e8ea", "#9ea2a8", "#2a2d31", "#3b3f45", "#6f747c",
			 "#8cb3ed", "#a3c4f3", "#0f1216",
			 "#63c27c", "#e3a03a", "#ff7c62" };
}

static QString gFamily;

enum State { Rest, Hover, Focus, Disabled, Selected, Error };

struct C
{
	QPainter& p;
	const Pal& c;

	QFont font(double px, int weight = 500, bool tnum = false) const
	{
		QFont f(gFamily);
		f.setPixelSize(int(std::lround(px)));
		f.setWeight(QFont::Weight(weight));
		f.setHintingPreference(QFont::PreferNoHinting);
		if (tnum)
			f.setFeature("tnum", 1);
		return f;
	}

	double textW(const QString& s, double px, int weight = 500, bool tnum = false) const
	{
		return QFontMetricsF(font(px, weight, tnum)).horizontalAdvance(s);
	}

	void text(QRectF r, const QString& s, double px, int weight, QColor col,
		int flags = Qt::AlignLeft | Qt::AlignVCenter, bool tnum = false)
	{
		p.setFont(font(px, weight, tnum));
		p.setPen(col);
		p.drawText(r, flags | Qt::TextSingleLine, s);
	}

	void box(QRectF r, QColor fill, QColor border, double radius, double bw = 1, Qt::PenStyle style = Qt::SolidLine)
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		if (border.isValid() && bw > 0)
		{
			QPen pen(border, bw, style);
			if (style == Qt::DashLine)
				pen.setDashPattern({ 3, 3 });
			p.setPen(pen);
			const double h = bw / 2;
			r = r.adjusted(h, h, -h, -h);
			radius = qMax(0.0, radius - h);
		}
		else
			p.setPen(Qt::NoPen);
		p.setBrush(fill.isValid() ? QBrush(fill) : Qt::NoBrush);
		p.drawRoundedRect(r, radius, radius);
		p.restore();
	}

	void hline(double x1, double x2, double y, QColor col)
	{
		p.fillRect(QRectF(x1, y, x2 - x1, 1), col);
	}

	void vline(double x, double y1, double y2, QColor col)
	{
		p.fillRect(QRectF(x, y1, 1, y2 - y1), col);
	}

	QColor faded(QColor col, double a) const
	{
		col.setAlphaF(a);
		return col;
	}

	// ---- stroke glyphs on a 16 px grid ---------------------------------
	void icon(const QString& name, QRectF r, QColor col, double sw = 1.5)
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		const double s = qMin(r.width(), r.height()) / 16.0;
		p.translate(r.center().x() - 8 * s, r.center().y() - 8 * s);
		p.scale(s, s);
		QPen pen(col, sw / s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		QPainterPath a;
		auto L = [&](std::initializer_list<QPointF> pts) {
				bool first = true;
				for (const QPointF& q : pts)
				{
					if (first)
						a.moveTo(q);
					else
						a.lineTo(q);
					first = false;
				}
			};
		if (name == "new")
		{
			L({ { 9, 2 }, { 4, 2 }, { 4, 14 }, { 12, 14 }, { 12, 5 }, { 9, 2 }, { 9, 5 }, { 12, 5 } });
			L({ { 8, 8 }, { 8, 12 } });
			L({ { 6, 10 }, { 10, 10 } });
		}
		else if (name == "open")
		{
			L({ { 2, 12.5 }, { 2, 3.5 }, { 6, 3.5 }, { 7.5, 5.5 }, { 14, 5.5 }, { 14, 12.5 }, { 2, 12.5 } });
		}
		else if (name == "save")
		{
			L({ { 3, 3 }, { 11, 3 }, { 13, 5 }, { 13, 13 }, { 3, 13 }, { 3, 3 } });
			L({ { 5.5, 3 }, { 5.5, 6 }, { 10, 6 }, { 10, 3 } });
			L({ { 5.5, 13 }, { 5.5, 9.5 }, { 10.5, 9.5 }, { 10.5, 13 } });
		}
		else if (name == "undo" || name == "redo")
		{
			if (name == "redo")
			{
				p.translate(16, 0);
				p.scale(-1, 1);
			}
			L({ { 6.5, 3 }, { 3.5, 6 }, { 6.5, 9 } });
			a.moveTo(3.5, 6);
			a.lineTo(9.5, 6);
			a.arcTo(QRectF(6, 6, 7, 7), 90, -180);
			a.lineTo(6, 13);
		}
		else if (name == "chev-down")
			L({ { 4.5, 6.5 }, { 8, 10 }, { 11.5, 6.5 } });
		else if (name == "chev-right")
			L({ { 6.5, 4.5 }, { 10, 8 }, { 6.5, 11.5 } });
		else if (name == "plus")
		{
			L({ { 8, 3.5 }, { 8, 12.5 } });
			L({ { 3.5, 8 }, { 12.5, 8 } });
		}
		else if (name == "trash")
		{
			L({ { 3, 4.5 }, { 13, 4.5 } });
			L({ { 6.5, 4.5 }, { 6.5, 2.5 }, { 9.5, 2.5 }, { 9.5, 4.5 } });
			L({ { 4.5, 4.5 }, { 5, 13.5 }, { 11, 13.5 }, { 11.5, 4.5 } });
			L({ { 7, 7 }, { 7, 11 } });
			L({ { 9, 7 }, { 9, 11 } });
		}
		else if (name == "code")
		{
			L({ { 5.5, 4.5 }, { 2, 8 }, { 5.5, 11.5 } });
			L({ { 10.5, 4.5 }, { 14, 8 }, { 10.5, 11.5 } });
			L({ { 9, 3.5 }, { 7, 12.5 } });
		}
		else if (name == "search")
		{
			a.addEllipse(QPointF(7, 7), 4, 4);
			L({ { 10, 10 }, { 13.5, 13.5 } });
		}
		else if (name == "close")
		{
			L({ { 4, 4 }, { 12, 12 } });
			L({ { 12, 4 }, { 4, 12 } });
		}
		else if (name == "min")
			L({ { 4, 8 }, { 12, 8 } });
		else if (name == "max")
			a.addRect(QRectF(4.5, 4.5, 7, 7));
		else if (name == "check")
			L({ { 3.5, 8.5 }, { 6.5, 11.5 }, { 12.5, 4.5 } });
		else if (name == "channels")
		{
			L({ { 2.5, 4 }, { 9, 4 } });
			L({ { 2.5, 8 }, { 7, 8 } });
			L({ { 2.5, 12 }, { 6, 12 } });
			L({ { 9, 10.5 }, { 11, 12.5 }, { 14, 8 } });
		}
		else if (name == "volume")
			L({ { 2.5, 12.5 }, { 13.5, 12.5 }, { 13.5, 4 }, { 2.5, 12.5 } });
		else if (name == "hp")
		{
			a.moveTo(2, 13);
			a.cubicTo(5, 13, 5.5, 5, 9, 5);
			a.lineTo(14, 5);
		}
		else if (name == "ls")
		{
			a.moveTo(2, 5);
			a.lineTo(5, 5);
			a.cubicTo(8, 5, 8, 11, 11, 11);
			a.lineTo(14, 11);
		}
		else if (name == "pk")
		{
			a.moveTo(2, 11);
			a.lineTo(4, 11);
			a.cubicTo(6.5, 11, 6.5, 4, 8, 4);
			a.cubicTo(9.5, 4, 9.5, 11, 12, 11);
			a.lineTo(14, 11);
		}
		else if (name == "file")
		{
			L({ { 9, 2 }, { 4, 2 }, { 4, 14 }, { 12, 14 }, { 12, 5 }, { 9, 2 }, { 9, 5 }, { 12, 5 } });
			L({ { 6, 8.5 }, { 10, 8.5 } });
			L({ { 6, 11 }, { 10, 11 } });
		}
		else if (name == "wave")
		{
			L({ { 2.5, 7 }, { 2.5, 9 } });
			L({ { 5, 3.5 }, { 5, 12.5 } });
			L({ { 7.5, 5.5 }, { 7.5, 10.5 } });
			L({ { 10, 6.5 }, { 10, 9.5 } });
			L({ { 12.5, 7.3 }, { 12.5, 8.7 } });
		}
		else if (name == "clock")
		{
			a.addEllipse(QPointF(8, 8), 5.5, 5.5);
			L({ { 8, 5 }, { 8, 8 }, { 10.5, 9.5 } });
		}
		else if (name == "copy")
		{
			L({ { 2.5, 5 }, { 5, 5 }, { 11, 11 }, { 13.5, 11 } });
			L({ { 2.5, 11 }, { 5, 11 }, { 6.6, 9.4 } });
			L({ { 9.4, 6.6 }, { 11, 5 }, { 13.5, 5 } });
			L({ { 12, 3.5 }, { 13.5, 5 }, { 12, 6.5 } });
			L({ { 12, 9.5 }, { 13.5, 11 }, { 12, 12.5 } });
		}
		else if (name == "plug")
		{
			L({ { 6, 2.5 }, { 6, 5.5 } });
			L({ { 10, 2.5 }, { 10, 5.5 } });
			L({ { 4, 5.5 }, { 12, 5.5 }, { 12, 8 } });
			a.moveTo(12, 8);
			a.arcTo(QRectF(4, 4, 8, 8), 0, -180);
			a.lineTo(4, 5.5);
			L({ { 8, 12 }, { 8, 14 } });
		}
		else if (name == "warn")
		{
			L({ { 8, 2.5 }, { 14, 13 }, { 2, 13 }, { 8, 2.5 } });
			L({ { 8, 6.5 }, { 8, 9.5 } });
			L({ { 8, 11.3 }, { 8, 11.4 } });
		}
		p.drawPath(a);
		p.restore();
	}

	// ---- controls -------------------------------------------------------
	// kind: 0 default, 1 primary, 2 quiet
	double button(QPointF pos, const QString& label, int kind = 0, State st = Rest, double h = 32,
		const QString& iconName = QString())
	{
		const double padX = kind == 2 ? 8 : 14;
		const double tw = textW(label, 14, 500);
		const double iw = iconName.isEmpty() ? 0 : 22;
		const QRectF r(pos.x(), pos.y(), tw + iw + padX * 2, h);
		p.save();
		if (st == Disabled && kind != 1)
			p.setOpacity(0.45);
		QColor ink = c.text;
		if (kind == 0)
			box(r, st == Hover ? c.sunk : c.surface, st == Hover ? c.edge : (st == Focus ? c.mark : c.lineStrong), 6,
				st == Focus ? 2 : 1);
		else if (kind == 1)
		{
			if (st == Disabled)
				box(r, QColor(), c.edge, 6); // outline, never a dimmed fill
			else
			{
				box(r, st == Hover ? c.markHover : c.mark, QColor(), 6);
				ink = c.markText;
			}
		}
		else
		{
			if (st == Hover)
				box(r, c.sunk, QColor(), 6);
			ink = c.dark ? c.mark : c.text;
		}
		if (!iconName.isEmpty())
			icon(iconName, QRectF(r.x() + padX - 2, r.center().y() - 8, 16, 16), ink);
		const QRectF tr(r.x() + padX + iw, r.y(), tw + 2, h);
		text(tr, label, 14, 500, ink);
		if (kind == 2)
		{
			const double uy = std::floor(r.center().y() + 10);
			p.fillRect(QRectF(tr.x(), uy, tw, 1), faded(ink, st == Hover ? 1.0 : 0.45));
		}
		p.restore();
		return r.width();
	}

	void sw(QPointF pos, bool on, State st = Rest)
	{
		p.save();
		if (st == Disabled)
			p.setOpacity(0.45);
		const QRectF r(pos.x(), pos.y(), 36, 20);
		if (on)
			box(r, c.mark, st == Focus ? c.text : QColor(), 10, st == Focus ? 2 : 0);
		else
			box(r, c.surface, st == Focus ? c.mark : c.edge, 10, st == Focus ? 2 : 1);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(on ? c.markText : c.muted);
		p.drawEllipse(QPointF(pos.x() + (on ? 26 : 10), pos.y() + 10), 7, 7);
		p.restore();
	}

	void select(QRectF r, const QString& value, State st = Rest, bool quiet = false)
	{
		p.save();
		if (st == Disabled)
			p.setOpacity(0.45);
		if (!quiet)
			box(r, c.surface, st == Focus ? c.mark : c.edge, 6, st == Focus ? 2 : 1);
		else if (st == Hover)
			box(r, c.sunk, QColor(), 6);
		const double px = quiet ? 13 : 14;
		text(r.adjusted(quiet ? 2 : 10, 0, -26, 0), value, px, 500, quiet ? c.muted : c.text);
		icon("chev-down", QRectF(r.right() - 24, r.center().y() - 8, 16, 16), c.muted);
		p.restore();
	}

	void field(QRectF r, const QString& value, const QString& unit = QString(), State st = Rest)
	{
		p.save();
		if (st == Disabled)
			p.setOpacity(0.45);
		QColor edge = c.edge;
		double bw = 1;
		if (st == Focus)
		{
			edge = c.mark;
			bw = 2;
		}
		else if (st == Error)
			edge = c.error;
		box(r, c.surface, edge, 6, bw);
		const double vw = textW(value, 14, 500, true);
		text(r.adjusted(10, 0, -8, 0), value, 14, 500, c.text, Qt::AlignLeft | Qt::AlignVCenter, true);
		if (!unit.isEmpty())
			text(QRectF(r.x() + 10 + vw + 5, r.y(), 60, r.height()), unit, 13, 500, c.muted);
		p.restore();
	}

	double tag(QPointF pos, const QString& s, bool dashed = false, QColor ink = QColor())
	{
		const double w = textW(s, 13, 500) + 16;
		const QRectF r(pos.x(), pos.y(), w, 22);
		box(r, QColor(), dashed ? c.edge : c.lineStrong, 11, 1, dashed ? Qt::DashLine : Qt::SolidLine);
		text(r, s, 13, 500, ink.isValid() ? ink : c.text, Qt::AlignCenter);
		return w;
	}

	// a checkable chip: outline when off, mark fill when on
	double chip(QPointF pos, const QString& s, bool on, State st = Rest, double minW = 40)
	{
		const double w = qMax(minW, textW(s, 14, 500) + 24);
		const QRectF r(pos.x(), pos.y(), w, 28);
		p.save();
		if (st == Disabled)
			p.setOpacity(0.45);
		if (on)
			box(r, c.mark, QColor(), 6);
		else
			box(r, st == Hover ? c.sunk : c.surface, c.lineStrong, 6);
		text(r, s, 14, on ? 600 : 500, on ? c.markText : c.text, Qt::AlignCenter, true);
		p.restore();
		return w;
	}

	void lamp(QPointF center, QColor col, bool hollow = false)
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		if (hollow)
		{
			p.setPen(QPen(col, 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, 3.25, 3.25);
		}
		else
		{
			p.setPen(Qt::NoPen);
			p.setBrush(col);
			p.drawEllipse(center, 4, 4);
		}
		p.restore();
	}

	// A plain dial: thin track, value arc in the emphasis colour, a flat face
	// with one pointer line. frac 0..1 over a 270 degree sweep.
	void knob(QPointF ctr, double frac, bool bipolar, State st = Rest)
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		if (st == Disabled)
			p.setOpacity(0.45);
		const double R = 23; // track radius
		const QRectF tr(ctr.x() - R, ctr.y() - R, 2 * R, 2 * R);
		const double start = 225, sweep = -270; // degrees, Qt: ccw positive
		QPen track(c.lineStrong, 3, Qt::SolidLine, Qt::RoundCap);
		p.setPen(track);
		p.setBrush(Qt::NoBrush);
		p.drawArc(tr, int(start * 16), int(sweep * 16));
		QPen val(st == Disabled ? c.muted : c.mark, 3, Qt::SolidLine, Qt::RoundCap);
		p.setPen(val);
		const double ang = start + sweep * frac;
		if (bipolar)
		{
			if (std::abs(frac - 0.5) > 0.004)
				p.drawArc(tr, 90 * 16, int((ang - 90) * 16));
			// the centre detent: a short tick across the track at 12 o'clock
			p.setPen(QPen(c.text, 1.5, Qt::SolidLine, Qt::FlatCap));
			p.drawLine(QPointF(ctr.x(), ctr.y() - R - 3.5), QPointF(ctr.x(), ctr.y() - R + 3.5));
		}
		else if (frac > 0.004)
			p.drawArc(tr, int(start * 16), int((sweep * frac) * 16));
		// face
		const double fr = 16;
		QColor edge = c.edge;
		double bw = 1;
		if (st == Focus)
		{
			edge = c.mark;
			bw = 2;
		}
		p.setPen(QPen(edge, bw));
		p.setBrush(st == Hover ? c.sunk : c.surface);
		p.drawEllipse(ctr, fr - bw / 2, fr - bw / 2);
		const double rad = ang * M_PI / 180.0;
		const QPointF dir(std::cos(rad), -std::sin(rad));
		p.setPen(QPen(c.text, 2, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(ctr + dir * 6.0, ctr + dir * 12.0);
		p.restore();
	}

	void segmented(QRectF r, const QStringList& items, int chosen)
	{
		box(r, c.surface, c.lineStrong, 6);
		const double w = r.width() / items.size();
		for (int i = 0; i < items.size(); i++)
		{
			const QRectF cell(r.x() + i * w, r.y(), w, r.height());
			if (i == chosen)
				box(cell, c.mark, QColor(), 6);
			else if (i > 0 && i - 1 != chosen)
				vline(std::floor(cell.x()), r.y() + 7, r.bottom() - 7, c.lineStrong);
			text(cell, items[i], 14, i == chosen ? 600 : 500, i == chosen ? c.markText : c.text, Qt::AlignCenter);
		}
	}

	void checkbox(QPointF pos, bool on, const QString& label, bool radio = false)
	{
		const QRectF r(pos.x(), pos.y(), 18, 18);
		if (radio)
		{
			box(r, c.surface, on ? c.mark : c.edge, 9, on ? 2 : 1);
			if (on)
				box(r.adjusted(5, 5, -5, -5), c.mark, QColor(), 4);
		}
		else if (on)
		{
			box(r, c.mark, QColor(), 4);
			icon("check", r.adjusted(1, 1, -1, -1), c.markText, 1.8);
		}
		else
			box(r, c.surface, c.edge, 4);
		text(QRectF(pos.x() + 26, pos.y() - 3, 300, 24), label, 14, 500, c.text);
	}

	void shadow(QRectF r, double radius)
	{
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		for (int i = 10; i >= 1; i--)
		{
			p.setBrush(QColor(0, 0, 0, c.dark ? 9 : 4));
			p.drawRoundedRect(r.adjusted(-i, -i + 4, i, i + 4), radius + i, radius + i);
		}
		p.restore();
	}
};

// ---- window chrome shared by the scenes -----------------------------------

static void chrome(C& g, const QString& tabName, const QStringList& otherTabs, bool saved)
{
	const Pal& c = g.c;
	const double W = 1600;
	g.p.fillRect(QRectF(0, 0, W, 1000), c.bg);
	// title bar
	g.text(QRectF(14, 0, 600, 36), QStringLiteral("Equalizer APO 2.69 구성 편집기"), 13, 500, c.text);
	g.icon("min", QRectF(W - 132, 10, 16, 16), c.text, 1.2);
	g.icon("max", QRectF(W - 86, 10, 16, 16), c.text, 1.2);
	g.icon("close", QRectF(W - 40, 10, 16, 16), c.text, 1.2);
	// menu bar
	double x = 14;
	for (const QString& m : { QStringLiteral("파일"), QStringLiteral("편집"), QStringLiteral("보기"), QStringLiteral("설정") })
	{
		const double w = g.textW(m, 14, 500) + 20;
		g.text(QRectF(x, 36, w, 30), m, 14, 500, c.text, Qt::AlignCenter);
		x += w + 4;
	}
	// toolbar on the surface, a hairline above and below
	g.p.fillRect(QRectF(0, 68, W, 52), c.surface);
	g.hline(0, W, 68, c.line);
	g.hline(0, W, 120, c.line);
	x = 18;
	for (const QString& n : { QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("save") })
	{
		g.icon(n, QRectF(x, 85, 18, 18), c.text);
		x += 36;
	}
	g.vline(x + 2, 84, 104, c.line);
	x += 18;
	g.icon("undo", QRectF(x, 85, 18, 18), c.text);
	x += 36;
	g.icon("redo", QRectF(x, 85, 18, 18), g.faded(c.text, 0.45));
	x += 36;
	g.vline(x + 2, 84, 104, c.line);
	x += 20;
	g.sw(QPointF(x, 84), true);
	g.text(QRectF(x + 46, 78, 120, 32), QStringLiteral("즉시 적용"), 14, 500, c.text);
	x += 46 + g.textW(QStringLiteral("즉시 적용"), 14) + 24;
	g.lamp(QPointF(x + 4, 94), saved ? c.success : c.warning);
	g.text(QRectF(x + 16, 78, 200, 32), saved ? QStringLiteral("저장됨") : QStringLiteral("저장 안 됨"), 14, 500, c.text);
	// device and channel selects, right
	g.select(QRectF(W - 196, 78, 180, 32), QStringLiteral("스테레오"));
	g.text(QRectF(W - 196 - 44, 78, 36, 32), QStringLiteral("채널"), 14, 500, c.muted, Qt::AlignRight | Qt::AlignVCenter);
	g.select(QRectF(W - 196 - 64 - 360, 78, 360, 32), QStringLiteral("스피커 (TOPPING USB DAC)"));
	g.text(QRectF(W - 196 - 64 - 360 - 44, 78, 36, 32), QStringLiteral("장치"), 14, 500, c.muted, Qt::AlignRight | Qt::AlignVCenter);
	// file tabs
	g.hline(0, W, 160, c.line);
	x = 14;
	const double tw = g.textW(tabName, 14, 600) + 24;
	g.text(QRectF(x, 121, tw, 39), tabName, 14, 600, c.text, Qt::AlignCenter);
	g.p.fillRect(QRectF(x + 12, 158, tw - 24, 2), c.mark);
	x += tw + 4;
	for (const QString& t : otherTabs)
	{
		const double w = g.textW(t, 14, 500) + 24;
		g.text(QRectF(x, 121, w, 39), t, 14, 500, c.muted, Qt::AlignCenter);
		x += w + 4;
	}
}

struct CardOpt
{
	int number;
	QString typeIcon;
	QString title;
	bool on = true;
	State st = Rest;
	bool expanded = true;
	QStringList channels = { "L", "R" };
};

// Header order is the shared contract: [expand][number][power][+][remove]
// [edit][type][title][channel tags][space]. Returns the body rect.
static QRectF card(C& g, QRectF r, const CardOpt& o)
{
	const Pal& c = g.c;
	QColor border = c.line;
	if (o.st == Hover)
		border = c.lineStrong;
	if (o.st == Selected)
		border = c.mark;
	g.box(r, c.surface, border, 10);
	const double hy = r.y(), hh = 44;
	double x = r.x() + 12;
	g.icon(o.expanded ? "chev-down" : "chev-right", QRectF(x, hy + 14, 16, 16), c.muted);
	x += 24;
	g.text(QRectF(x, hy, 22, hh), QString::number(o.number), 14, 500, c.muted, Qt::AlignCenter, true);
	x += 32;
	g.sw(QPointF(x, hy + 12), o.on);
	x += 36 + 14;
	for (const QString& n : { QStringLiteral("plus"), QStringLiteral("trash"), QStringLiteral("code") })
	{
		g.icon(n, QRectF(x, hy + 14, 16, 16), c.muted);
		x += 30;
	}
	x += 8;
	const QColor ink = o.on ? c.text : c.muted;
	g.icon(o.typeIcon, QRectF(x, hy + 13, 18, 18), ink);
	x += 28;
	const double tw = g.textW(o.title, 15, 600);
	g.text(QRectF(x, hy, tw + 4, hh), o.title, 15, 600, ink);
	x += tw + 16;
	for (const QString& ch : o.channels)
		x += g.tag(QPointF(x, hy + 11), ch, false, ink) + 4;
	if (!o.expanded)
		return QRectF();
	g.hline(r.x() + 1, r.right() - 1, hy + hh, c.line);
	return QRectF(r.x() + 16, hy + hh + 1, r.width() - 32, r.height() - hh - 1);
}

// knob, then to its right the parameter name over its value field
static double knobField(C& g, QPointF pos, const QString& label, const QString& value, const QString& unit,
	double frac, bool bipolar, double fieldW = 150, State st = Rest, bool labelIsSelect = true, State fieldSt = Rest)
{
	g.knob(QPointF(pos.x() + 28, pos.y() + 33), frac, bipolar, st);
	const double fx = pos.x() + 66;
	if (labelIsSelect)
		g.select(QRectF(fx, pos.y(), fieldW, 26), label, st == Disabled ? Disabled : Rest, true);
	else
	{
		g.p.save();
		if (st == Disabled)
			g.p.setOpacity(0.45);
		g.text(QRectF(fx + 2, pos.y(), fieldW, 26), label, 13, 500, g.c.muted);
		g.p.restore();
	}
	g.field(QRectF(fx, pos.y() + 30, fieldW, 32), value, unit, st == Disabled ? Disabled : fieldSt);
	return 66 + fieldW;
}

static double response(double f)
{
	const double hp = -10 * std::log10(1 + std::pow(30 / f, 4));
	const double shelf = 4.2 / (1 + std::pow(f / 110, 2));
	const double pk = -4.0 * std::exp(-std::pow(std::log2(f / 250), 2) / (2 * 0.33 * 0.33));
	const double pk2 = -1.6 * std::exp(-std::pow(std::log2(f / 3200), 2) / (2 * 0.8 * 0.8));
	const double pk3 = 0.9 * std::exp(-std::pow(std::log2(f / 900), 2) / (2 * 0.5 * 0.5));
	return -2.0 + hp + shelf + pk + pk2 + pk3;
}

static void graphDock(C& g, double top)
{
	const Pal& c = g.c;
	const double W = 1600, H = 1000;
	g.p.fillRect(QRectF(0, top, W, H - top), c.bg);
	g.hline(0, W, top, c.line);
	g.text(QRectF(16, top + 1, 200, 34), QStringLiteral("그래프"), 14, 600, c.text);
	// left: the settings column
	const double lx = 16, ly = top + 40, lw = 250;
	const QStringList labels = { QStringLiteral("소스"), QStringLiteral("채널"), QStringLiteral("해상도"), QStringLiteral("위치") };
	const QStringList values = { QStringLiteral("현재 파일"), QStringLiteral("L"), QStringLiteral("262144"), QStringLiteral("아래") };
	for (int i = 0; i < 4; i++)
	{
		g.text(QRectF(lx, ly + i * 36, 56, 32), labels[i], 14, 500, c.muted);
		if (i == 2)
			g.field(QRectF(lx + 64, ly + i * 36, lw - 64, 32), values[i]);
		else
			g.select(QRectF(lx + 64, ly + i * 36, lw - 64, 32), values[i]);
	}
	g.segmented(QRectF(lx, ly + 150, lw, 32), { QStringLiteral("크기"), QStringLiteral("위상"), QStringLiteral("군지연") }, 0);
	// facts: name left, number right, a hairline under each
	const QStringList fn = { QStringLiteral("피크"), QStringLiteral("지연"), QStringLiteral("초기화"), QStringLiteral("CPU") };
	const QStringList fv = { QStringLiteral("+0.8 dB"), QStringLiteral("10.0 ms"), QStringLiteral("19.6 ms"), QStringLiteral("0.4 %") };
	for (int i = 0; i < 4; i++)
	{
		const double col = lx + (i % 2) * (lw / 2 + 6);
		const double row = ly + 188 + (i / 2) * 30;
		const double cw = lw / 2 - 6;
		g.text(QRectF(col, row, cw, 30), fn[i], 13, 500, c.muted);
		g.text(QRectF(col, row, cw, 30), fv[i], 14, 600, i == 0 ? c.warning : c.text, Qt::AlignRight | Qt::AlignVCenter, true);
		g.hline(col, col + cw, row + 29, c.line);
	}
	// right: the plot on a surface panel
	const QRectF panel(lx + lw + 16, top + 40, W - (lx + lw + 16) - 16, H - top - 40 - 14);
	g.box(panel, c.surface, c.line, 10);
	const QRectF plot(panel.x() + 52, panel.y() + 40, panel.width() - 52 - 20, panel.height() - 40 - 34);
	auto X = [&](double f) { return plot.x() + plot.width() * std::log10(f / 20.0) / 3.0; };
	auto Y = [&](double db) { return plot.y() + plot.height() * (12 - db) / 24.0; };
	for (double db : { 12.0, 6.0, 0.0, -6.0, -12.0 })
	{
		const double y = std::floor(Y(db));
		g.hline(plot.x(), plot.right(), y, db == 0 ? c.edge : c.line);
		const QString s = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
		g.text(QRectF(panel.x(), y - 10, 44, 20), s, 13, 500, c.muted, Qt::AlignRight | Qt::AlignVCenter, true);
	}
	const QList<QPair<double, QString> > ticks = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" },
												   { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" },
												   { 10000, "10k" }, { 20000, "20k" } };
	for (const auto& t : ticks)
	{
		const double x = std::floor(X(t.first));
		g.vline(x, plot.y(), plot.bottom(), c.line);
		g.text(QRectF(x - 30, plot.bottom() + 6, 60, 20), t.second, 13, 500, c.muted, Qt::AlignCenter, true);
	}
	g.text(QRectF(plot.right() - 200, panel.y() + 8, 200, 24), QStringLiteral("L, 48000 Hz"), 13, 500, c.muted,
		Qt::AlignRight | Qt::AlignVCenter, true);
	// the over-0 notice: the state colour on the mark and nowhere else
	g.icon("warn", QRectF(plot.x(), panel.y() + 12, 16, 16), c.warning);
	g.text(QRectF(plot.x() + 24, panel.y() + 8, 700, 24),
		QStringLiteral("0 dB를 넘습니다. 소리가 일그러질 수 있으니 프리앰프를 0.8 dB 낮추십시오."), 14, 500, c.text);
	// the curve: one line in the emphasis colour; the part over 0 dB in the warning colour
	QPainterPath path;
	for (int i = 0; i <= 600; i++)
	{
		const double f = 20 * std::pow(1000.0, i / 600.0);
		const QPointF pt(X(f), Y(qBound(-12.0, response(f), 12.0)));
		if (i == 0)
			path.moveTo(pt);
		else
			path.lineTo(pt);
	}
	g.p.save();
	g.p.setRenderHint(QPainter::Antialiasing, true);
	g.p.setBrush(Qt::NoBrush);
	g.p.setClipRect(QRectF(plot.x(), Y(0), plot.width(), plot.bottom() - Y(0) + 2));
	g.p.setPen(QPen(c.mark, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	g.p.drawPath(path);
	g.p.setClipRect(QRectF(plot.x(), plot.y() - 2, plot.width(), Y(0) - plot.y() + 2));
	g.p.setPen(QPen(c.warning, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	g.p.drawPath(path);
	g.p.restore();
}

// ---- scene A: the everyday editor -----------------------------------------

static void sceneMain(C& g)
{
	const Pal& c = g.c;
	chrome(g, QStringLiteral("config.txt"), { QStringLiteral("game.txt"), QStringLiteral("movie.txt") }, true);
	const double x0 = 16, w = 1600 - 32 - 14;
	double y = 172;
	// 1 channel
	{
		const QRectF body = card(g, QRectF(x0, y, w, 96), { 1, "channels", QStringLiteral("채널") });
		double x = body.x();
		const QStringList chips = { "ALL", "L", "R", "3", "4", "5", "6", "7", "8" };
		for (int i = 0; i < chips.size(); i++)
			x += g.chip(QPointF(x, body.y() + 12), chips[i], i == 1 || i == 2, Rest, i == 0 ? 52 : 40) + 6;
		g.button(QPointF(x + 6, body.y() + 10), QStringLiteral("채널 추가"), 2, Rest, 32);
		y += 96 + 8;
	}
	// the cards under the channel command sit behind one quiet rail
	g.p.fillRect(QRectF(x0 + 6, y, 2, 1000), c.lineStrong);
	const double ix = x0 + 20, iw = w - 20;
	{
		const QRectF body = card(g, QRectF(ix, y, iw, 132), { 2, "volume", QStringLiteral("프리앰프") });
		knobField(g, QPointF(body.x(), body.y() + 12), QStringLiteral("게인"), QStringLiteral("-2.0"), "dB", 0.43, true, 150, Rest, false);
		y += 132 + 8;
	}
	{
		const QRectF body = card(g, QRectF(ix, y, iw, 132), { 3, "hp", QStringLiteral("하이패스 필터") });
		g.text(QRectF(body.x() + 2, body.y() + 12, 200, 26), QStringLiteral("종류"), 13, 500, c.muted);
		g.select(QRectF(body.x(), body.y() + 42, 240, 32), QStringLiteral("하이패스 필터"));
		double x = body.x() + 240 + 32;
		x += knobField(g, QPointF(x, body.y() + 12), QStringLiteral("코너 주파수"), QStringLiteral("30.00"), "Hz", 0.12, false) + 32;
		knobField(g, QPointF(x, body.y() + 12), QStringLiteral("Q 값"), QStringLiteral("0.7100"), "", 0.30, false);
		y += 132 + 8;
	}
	{
		const QRectF body = card(g, QRectF(ix, y, iw, 132), { 4, "ls", QStringLiteral("로우셸프 필터"), true, Selected });
		g.text(QRectF(body.x() + 2, body.y() + 12, 200, 26), QStringLiteral("종류"), 13, 500, c.muted);
		g.select(QRectF(body.x(), body.y() + 42, 240, 32), QStringLiteral("로우셸프 필터"));
		double x = body.x() + 240 + 32;
		x += knobField(g, QPointF(x, body.y() + 12), QStringLiteral("코너 주파수"), QStringLiteral("100.00"), "Hz", 0.28, false) + 32;
		x += knobField(g, QPointF(x, body.y() + 12), QStringLiteral("게인"), QStringLiteral("+3.0"), "dB", 0.60, true, 150, Rest, false, Focus) + 32;
		knobField(g, QPointF(x, body.y() + 12), QStringLiteral("Q 값"), QStringLiteral("0.7100"), "", 0.30, false);
		y += 132 + 8;
	}
	{
		const QRectF body = card(g, QRectF(ix, y, iw, 132), { 5, "pk", QStringLiteral("피킹 필터"), true, Hover });
		g.text(QRectF(body.x() + 2, body.y() + 12, 200, 26), QStringLiteral("종류"), 13, 500, c.muted);
		g.select(QRectF(body.x(), body.y() + 42, 240, 32), QStringLiteral("피킹 필터"));
		double x = body.x() + 240 + 32;
		x += knobField(g, QPointF(x, body.y() + 12), QStringLiteral("중심 주파수"), QStringLiteral("250.00"), "Hz", 0.38, false) + 32;
		x += knobField(g, QPointF(x, body.y() + 12), QStringLiteral("게인"), QStringLiteral("-4.0"), "dB", 0.37, true, 150, Rest, false) + 32;
		knobField(g, QPointF(x, body.y() + 12), QStringLiteral("Q 값"), QStringLiteral("2.0000"), "", 0.52, false);
		y += 132 + 8;
	}
	// scroll bar
	g.box(QRectF(1600 - 12, 176, 6, 300), c.lineStrong, QColor(), 3);
	graphDock(g, 700);
}

// ---- scene B: references, a missing file, routing, a row switched off ------

static void sceneFiles(C& g)
{
	const Pal& c = g.c;
	chrome(g, QStringLiteral("surround.txt"), { QStringLiteral("config.txt") }, false);
	const double x0 = 16, w = 1600 - 32 - 14;
	double y = 172;
	{
		const QRectF body = card(g, QRectF(x0, y, w, 110), { 1, "file", QStringLiteral("포함") });
		g.text(QRectF(body.x(), body.y() + 10, 600, 24), QStringLiteral("hesuvi.txt"), 15, 600, c.text);
		g.text(QRectF(body.x(), body.y() + 34, 600, 22), QStringLiteral("config 폴더의 HeSuVi 안"), 13, 500, c.muted);
		double x = body.x() + qMax(g.textW(QStringLiteral("hesuvi.txt"), 15, 600), g.textW(QStringLiteral("config 폴더의 HeSuVi 안"), 13)) + 28;
		x += g.button(QPointF(x, body.y() + 16), QStringLiteral("열기")) + 8;
		g.button(QPointF(x, body.y() + 16), QStringLiteral("바꾸기"));
		y += 110 + 8;
	}
	{
		const QRectF body = card(g, QRectF(x0, y, w, 110), { 2, "wave", QStringLiteral("컨볼루션") });
		const QString name = QStringLiteral("BRIR_home_22ch_2s.wav");
		const QString why = QStringLiteral("파일을 찾을 수 없습니다. 옮겼거나 지운 파일이면 다시 고르십시오.");
		g.lamp(QPointF(body.x() + 5, body.y() + 22), c.error);
		g.text(QRectF(body.x() + 18, body.y() + 10, 600, 24), name, 15, 600, c.text);
		g.text(QRectF(body.x() + 18, body.y() + 34, 700, 22), why, 13, 500, c.text);
		const double x = body.x() + 18 + qMax(g.textW(name, 15, 600), g.textW(why, 13)) + 28;
		g.button(QPointF(x, body.y() + 16), QStringLiteral("파일 찾기"), 1);
		y += 110 + 8;
	}
	{
		const QRectF body = card(g, QRectF(x0, y, w, 110), { 3, "wave", QStringLiteral("컨볼루션") });
		const QString name = QStringLiteral("room_correction_L.wav");
		g.text(QRectF(body.x(), body.y() + 10, 600, 24), name, 15, 600, c.text);
		g.text(QRectF(body.x(), body.y() + 34, 600, 22), QStringLiteral("config 폴더의 IR 안"), 13, 500, c.muted);
		double x = body.x() + qMax(g.textW(name, 15, 600), g.textW(QStringLiteral("config 폴더의 IR 안"), 13)) + 28;
		x += g.tag(QPointF(x, body.y() + 21), QStringLiteral("48 kHz")) + 6;
		x += g.tag(QPointF(x, body.y() + 21), QStringLiteral("2채널")) + 6;
		x += g.tag(QPointF(x, body.y() + 21), QStringLiteral("65536 샘플")) + 20;
		g.button(QPointF(x, body.y() + 16), QStringLiteral("바꾸기"));
		y += 110 + 8;
	}
	{
		const QRectF body = card(g, QRectF(x0, y, w, 132), { 4, "clock", QStringLiteral("지연"), false });
		knobField(g, QPointF(body.x(), body.y() + 12), QStringLiteral("지연"), QStringLiteral("10.00"), "ms", 0.2, false, 150, Disabled, false);
		g.p.save();
		g.p.setOpacity(0.45);
		g.text(QRectF(body.x() + 248, body.y() + 12, 100, 26), QStringLiteral("단위"), 13, 500, c.muted);
		g.p.restore();
		g.select(QRectF(body.x() + 246, body.y() + 42, 120, 32), QStringLiteral("밀리초"), Disabled);
		y += 132 + 8;
	}
	{
		const QRectF body = card(g, QRectF(x0, y, w, 190), { 5, "copy", QStringLiteral("복사"), true, Rest, true, {} });
		const QStringList outs = { "L", "R" };
		const QList<QStringList> terms = { { "0.86", "L", "-0.50", "R" }, { "0.86", "R", "-0.50", "L" } };
		for (int i = 0; i < 2; i++)
		{
			const double ry = body.y() + 12 + i * 44;
			double x = body.x();
			x += g.tag(QPointF(x, ry + 5), outs[i]) + 10;
			g.text(QRectF(x, ry, 16, 32), "=", 15, 500, c.muted, Qt::AlignCenter);
			x += 26;
			for (int t = 0; t < 2; t++)
			{
				if (t > 0)
				{
					g.text(QRectF(x, ry, 16, 32), "+", 15, 500, c.muted, Qt::AlignCenter);
					x += 26;
				}
				g.field(QRectF(x, ry, 76, 32), terms[i][t * 2]);
				x += 76 + 8;
				g.text(QRectF(x, ry, 12, 32), QStringLiteral("×"), 14, 500, c.muted, Qt::AlignCenter);
				x += 20;
				g.select(QRectF(x, ry, 72, 32), terms[i][t * 2 + 1]);
				x += 72 + 10;
			}
			g.icon("plus", QRectF(x + 4, ry + 8, 16, 16), c.muted);
		}
		double x = body.x();
		x += g.button(QPointF(x, body.y() + 104), QStringLiteral("채널 6개 더 보기"), 2) + 12;
		g.button(QPointF(x, body.y() + 104), QStringLiteral("채널 추가"), 2);
		y += 190 + 8;
	}
	// the add row: always there, an ordinary button
	g.button(QPointF(x0, y + 4), QStringLiteral("명령 추가"), 0, Rest, 36, "plus");
	g.box(QRectF(1600 - 12, 176, 6, 520), c.lineStrong, QColor(), 3);
	// status bar
	g.hline(0, 1600, 966, c.line);
	g.text(QRectF(16, 967, 800, 33), QStringLiteral("명령 5개, 꺼진 명령 1개, 찾지 못한 파일 1개"), 13, 500, c.muted);
}

// ---- scene C: the parts ---------------------------------------------------

static void sceneParts(C& g)
{
	const Pal& c = g.c;
	g.p.fillRect(QRectF(0, 0, 1600, 1000), c.bg);
	auto title = [&](double x, double y, const QString& s) {
			g.text(QRectF(x, y, 400, 22), s, 14, 600, c.text);
		};
	auto cap = [&](double x, double y, const QString& s, double w = 140) {
			g.text(QRectF(x, y, w, 20), s, 13, 500, c.muted);
		};
	const double L = 32;
	double y = 28;

	title(L, y, QStringLiteral("단추"));
	{
		double x = L;
		const double by = y + 34, cy = y + 74;
		cap(x, cy, QStringLiteral("기본"));
		x += g.button(QPointF(x, by), QStringLiteral("바꾸기")) + 28;
		cap(x, cy, QStringLiteral("가리킴"));
		x += g.button(QPointF(x, by), QStringLiteral("바꾸기"), 0, Hover) + 28;
		cap(x, cy, QStringLiteral("포커스"));
		x += g.button(QPointF(x, by), QStringLiteral("바꾸기"), 0, Focus) + 28;
		cap(x, cy, QStringLiteral("주 동작"));
		x += g.button(QPointF(x, by), QStringLiteral("파일 찾기"), 1) + 28;
		cap(x, cy, QStringLiteral("주 동작, 쓸 수 없음"), 200);
		x += qMax(g.button(QPointF(x, by), QStringLiteral("파일 찾기"), 1, Disabled), 130.0) + 28;
		cap(x, cy, QStringLiteral("글 단추"));
		x += g.button(QPointF(x, by), QStringLiteral("채널 추가"), 2) + 28;
		cap(x, cy, QStringLiteral("쓸 수 없음"));
		g.button(QPointF(x, by), QStringLiteral("바꾸기"), 0, Disabled);
	}
	y += 108;
	title(L, y, QStringLiteral("스위치와 고르기"));
	{
		double x = L;
		const double by = y + 40, cy = y + 74;
		g.sw(QPointF(x, by), false);
		cap(x, cy, QStringLiteral("꺼짐"));
		x += 80;
		g.sw(QPointF(x, by), true);
		cap(x, cy, QStringLiteral("켜짐"));
		x += 80;
		g.sw(QPointF(x, by), false, Focus);
		cap(x, cy, QStringLiteral("포커스"));
		x += 80;
		g.sw(QPointF(x, by), true, Disabled);
		cap(x, cy, QStringLiteral("쓸 수 없음"));
		x += 110;
		g.checkbox(QPointF(x, by + 1), true, QStringLiteral("끝의 무음 무시"));
		x += 160;
		g.checkbox(QPointF(x, by + 1), false, QStringLiteral("버퍼 제거"));
		x += 130;
		g.checkbox(QPointF(x, by + 1), true, QStringLiteral("재생"), true);
		x += 80;
		g.checkbox(QPointF(x, by + 1), false, QStringLiteral("녹음"), true);
		x += 100;
		g.segmented(QRectF(x, by - 6, 250, 32), { QStringLiteral("크기"), QStringLiteral("위상"), QStringLiteral("군지연") }, 1);
		cap(x, cy, QStringLiteral("셋 중 하나"));
	}
	y += 108;
	title(L, y, QStringLiteral("고르는 칸과 입력칸"));
	{
		double x = L;
		const double by = y + 34, cy = y + 74;
		g.select(QRectF(x, by, 200, 32), QStringLiteral("하이패스 필터"));
		cap(x, cy, QStringLiteral("고르는 칸"));
		x += 228;
		g.select(QRectF(x, by, 200, 32), QStringLiteral("하이패스 필터"), Focus);
		cap(x, cy, QStringLiteral("포커스"));
		x += 228;
		g.field(QRectF(x, by, 140, 32), QStringLiteral("100.00"), "Hz");
		cap(x, cy, QStringLiteral("입력칸"));
		x += 168;
		g.field(QRectF(x, by, 140, 32), QStringLiteral("100.00"), "Hz", Focus);
		cap(x, cy, QStringLiteral("포커스"));
		x += 168;
		g.field(QRectF(x, by, 140, 32), QStringLiteral("0.00"), "Hz", Error);
		cap(x, cy, QStringLiteral("받을 수 없는 값"));
		x += 168;
		g.field(QRectF(x, by, 140, 32), QStringLiteral("100.00"), "Hz", Disabled);
		cap(x, cy, QStringLiteral("쓸 수 없음"));
	}
	y += 108;
	title(L, y, QStringLiteral("이름표와 상태"));
	{
		double x = L;
		const double by = y + 38, cy = y + 74;
		const double x1 = x;
		x += g.tag(QPointF(x, by), "L") + 4;
		x += g.tag(QPointF(x, by), "R") + 4;
		x += g.tag(QPointF(x, by), QStringLiteral("48 kHz")) + 4;
		cap(x1, cy, QStringLiteral("이름표"));
		x += 36;
		g.tag(QPointF(x, by), QStringLiteral("가상 채널"), true);
		cap(x, cy, QStringLiteral("아직 없는 것"));
		x += 130;
		const QList<QPair<QColor, QString> > lamps = { { c.success, QStringLiteral("저장됨") }, { c.warning, QStringLiteral("저장 안 됨") },
													   { c.error, QStringLiteral("찾을 수 없음") } };
		for (const auto& l : lamps)
		{
			g.lamp(QPointF(x + 4, by + 11), l.first);
			g.text(QRectF(x + 16, by, 160, 22), l.second, 14, 500, c.text);
			x += g.textW(l.second, 14) + 44;
		}
		g.lamp(QPointF(x + 4, by + 11), c.muted, true);
		g.text(QRectF(x + 16, by, 160, 22), QStringLiteral("꺼짐"), 14, 500, c.muted);
		cap(x1 + 310, cy, QStringLiteral("상태는 점 하나와 낱말 하나"), 300);
		x += 100;
		// tabs
		g.hline(x, x + 260, by + 30, c.line);
		g.text(QRectF(x, by - 8, 90, 38), "config.txt", 14, 600, c.text, Qt::AlignCenter);
		g.p.fillRect(QRectF(x + 10, by + 28, 70, 2), c.mark);
		g.text(QRectF(x + 94, by - 8, 90, 38), "game.txt", 14, 500, c.muted, Qt::AlignCenter);
		cap(x, cy + 4, QStringLiteral("탭"));
	}
	y += 122;
	title(L, y, QStringLiteral("노브"));
	{
		double x = L;
		const double by = y + 36, cy = y + 108;
		const QStringList caps = { QStringLiteral("한쪽 값"), QStringLiteral("올림"), QStringLiteral("내림"), QStringLiteral("0 dB"),
								   QStringLiteral("가리킴"), QStringLiteral("포커스"), QStringLiteral("쓸 수 없음") };
		const double fr[] = { 0.28, 0.66, 0.34, 0.5, 0.66, 0.66, 0.66 };
		const bool bi[] = { false, true, true, true, true, true, true };
		const State st[] = { Rest, Rest, Rest, Rest, Hover, Focus, Disabled };
		const QStringList val = { "100.00", "+3.0", "-4.0", "0.0", "+3.0", "+3.0", "+3.0" };
		for (int i = 0; i < 7; i++)
		{
			knobField(g, QPointF(x, by), i == 0 ? QStringLiteral("코너 주파수") : QStringLiteral("게인"), val[i],
				i == 0 ? "Hz" : "dB", fr[i], bi[i], 92, st[i], false);
			cap(x, cy, caps[i]);
			x += 66 + 92 + 8;
		}
	}
	y += 152;
	title(L, y, QStringLiteral("카드"));
	{
		const double cw = 372;
		const double by = y + 34;
		const State sts[] = { Rest, Hover, Selected };
		const QStringList caps = { QStringLiteral("기본"), QStringLiteral("가리킴: 테두리가 한 단 진해집니다"),
								   QStringLiteral("고름: 테두리가 강조색이 됩니다") };
		for (int i = 0; i < 3; i++)
		{
			const double x = L + i * (cw + 20);
			const QRectF body = card(g, QRectF(x, by, cw, 132), { i + 1, "pk", QStringLiteral("피킹 필터"), true, sts[i] });
			double kx = body.x();
			kx += knobField(g, QPointF(kx, body.y() + 12), QStringLiteral("중심 주파수"), "250.00", "Hz", 0.38, false, 104) + 14;
			knobField(g, QPointF(kx, body.y() + 12), QStringLiteral("게인"), "-4.0", "dB", 0.37, true, 84, Rest, false);
			cap(x, by + 140, caps[i], cw);
		}
	}
	y += 34 + 132 + 46;
	{
		const double cw = 372;
		const QRectF body = card(g, QRectF(L, y, cw, 132), { 4, "pk", QStringLiteral("피킹 필터"), false });
		double kx = body.x();
		kx += knobField(g, QPointF(kx, body.y() + 12), QStringLiteral("중심 주파수"), "250.00", "Hz", 0.38, false, 104, Disabled) + 14;
		knobField(g, QPointF(kx, body.y() + 12), QStringLiteral("게인"), "-4.0", "dB", 0.37, true, 84, Disabled, false);
		cap(L, y + 140, QStringLiteral("꺼짐: 스위치가 꺼지고 안의 것이 흐려집니다"), cw);
		card(g, QRectF(L + cw + 20, y, cw, 46), { 5, "hp", QStringLiteral("하이패스 필터"), true, Rest, false });
		cap(L + cw + 20, y + 54, QStringLiteral("접음"), cw);
	}

	// right column: the command picker and a menu
	const double px = 1204;
	title(px, 28, QStringLiteral("명령 고르기"));
	{
		const QRectF r(px, 62, 364, 520);
		g.shadow(r, 10);
		g.box(r, c.surface, c.lineStrong, 10);
		g.box(QRectF(r.x() + 12, r.y() + 12, r.width() - 24, 36), c.surface, c.mark, 6, 2);
		g.icon("search", QRectF(r.x() + 22, r.y() + 22, 16, 16), c.muted);
		g.text(QRectF(r.x() + 46, r.y() + 12, 300, 36), QStringLiteral("필터"), 14, 500, c.text);
		const double caretX = r.x() + 46 + g.textW(QStringLiteral("필터"), 14) + 2;
		g.p.fillRect(QRectF(caretX, r.y() + 21, 1, 18), c.text);
		struct Row { QString cat, icon, name, desc; };
		const QList<Row> rows = {
			{ QStringLiteral("이퀄라이저"), "", "", "" },
			{ "", "pk", QStringLiteral("피킹 필터"), QStringLiteral("한 주파수 둘레를 올리거나 내립니다") },
			{ "", "ls", QStringLiteral("로우셸프 필터"), QStringLiteral("낮은 음 전체를 올리거나 내립니다") },
			{ "", "hp", QStringLiteral("하이패스 필터"), QStringLiteral("기준보다 낮은 음을 줄입니다") },
			{ QStringLiteral("파일"), "", "", "" },
			{ "", "wave", QStringLiteral("컨볼루션"), QStringLiteral("임펄스 응답 파일을 적용합니다") },
			{ "", "file", QStringLiteral("포함"), QStringLiteral("다른 설정 파일을 이 자리에 불러옵니다") },
			{ QStringLiteral("플러그인"), "", "", "" },
			{ "", "plug", QStringLiteral("VST 플러그인"), QStringLiteral("VST2나 VST3 플러그인을 불러옵니다") },
		};
		double ry = r.y() + 60;
		int idx = 0;
		for (const Row& row : rows)
		{
			if (!row.cat.isEmpty())
			{
				g.text(QRectF(r.x() + 20, ry + 4, 300, 28), row.cat, 13, 600, c.muted);
				ry += 32;
				continue;
			}
			const QRectF rr(r.x() + 8, ry, r.width() - 16, 52);
			if (idx == 1)
				g.box(rr, c.sunk, QColor(), 6);
			g.icon(row.icon, QRectF(rr.x() + 14, rr.y() + 17, 18, 18), c.text);
			g.text(QRectF(rr.x() + 46, rr.y() + 6, 290, 22), row.name, 14, 600, c.text);
			g.text(QRectF(rr.x() + 46, rr.y() + 26, 290, 20), row.desc, 13, 500, c.muted);
			ry += 54;
			idx++;
		}
	}
	title(px, 612, QStringLiteral("메뉴"));
	{
		const QRectF r(px, 646, 300, 222);
		g.shadow(r, 10);
		g.box(r, c.surface, c.lineStrong, 10);
		struct Item { QString name, key; bool sep, checked, current, off; };
		const QList<Item> items = {
			{ QStringLiteral("수정 취소"), "Ctrl+Z", false, false, false, false },
			{ QStringLiteral("다시 수정"), "Ctrl+Y", false, false, false, true },
			{ "", "", true, false, false, false },
			{ QStringLiteral("잘라내기"), "Ctrl+X", false, false, true, false },
			{ QStringLiteral("복사"), "Ctrl+C", false, false, false, false },
			{ QStringLiteral("붙여넣기"), "Ctrl+V", false, false, false, false },
			{ "", "", true, false, false, false },
			{ QStringLiteral("즉시 적용"), "", false, true, false, false },
		};
		double ry = r.y() + 6;
		for (const Item& it : items)
		{
			if (it.sep)
			{
				g.hline(r.x() + 6, r.right() - 6, ry + 4, c.line);
				ry += 9;
				continue;
			}
			const QRectF rr(r.x() + 6, ry, r.width() - 12, 32);
			if (it.current)
				g.box(rr, c.sunk, QColor(), 6);
			if (it.checked)
				g.icon("check", QRectF(rr.x() + 8, rr.y() + 8, 16, 16), c.text);
			g.text(rr.adjusted(32, 0, 0, 0), it.name, 14, 500, it.off ? g.faded(c.text, 0.45) : c.text);
			g.text(rr.adjusted(0, 0, -12, 0), it.key, 13, 500, it.off ? g.faded(c.muted, 0.45) : c.muted, Qt::AlignRight | Qt::AlignVCenter);
			ry += 32;
		}
	}
}

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 3)
		return 2;
	for (const QString& f : QDir(args[1]).entryList({ "Pretendard-*.otf" }, QDir::Files))
	{
		const int id = QFontDatabase::addApplicationFont(QDir(args[1]).filePath(f));
		if (id >= 0 && gFamily.isEmpty())
			gFamily = QFontDatabase::applicationFontFamilies(id).value(0);
	}
	if (gFamily.isEmpty())
	{
		fprintf(stderr, "no Pretendard in %s\n", qPrintable(args[1]));
		return 2;
	}
	QDir().mkpath(args[2]);
	struct Scene { const char* name; std::function<void(C&)> fn; };
	const QList<Scene> scenes = { { "main", sceneMain }, { "files", sceneFiles }, { "parts", sceneParts } };
	int failures = 0;
	for (int mode = 0; mode < 2; mode++)
	{
		const Pal pal = mode == 0 ? lightPal() : darkPal();
		for (const Scene& s : scenes)
		{
			QImage img(1600, 1000, QImage::Format_ARGB32_Premultiplied);
			QPainter p(&img);
			p.setRenderHint(QPainter::TextAntialiasing, true);
			C g{ p, pal };
			s.fn(g);
			p.end();
			const QString file = QDir(args[2]).filePath(QStringLiteral("soft_%1_%2.png").arg(mode == 0 ? "light" : "dark", s.name));
			if (!img.save(file))
				failures++;
		}
	}
	return failures == 0 ? 0 : 1;
}
