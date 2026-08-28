/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSubwooferRoutingCardView.h"

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QBoxLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStyle>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "RackSubwooferRoutingDetail.h"

using namespace RackSubwooferRoutingDetail;


// ---- RackCrossoverReadout -------------------------------------------------

RackCrossoverReadout::RackCrossoverReadout(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent), skinTokens(tokens)
{
	setObjectName(QStringLiteral("RackBassCrossoverReadout"));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	setAccessibleName(tr("Crossover corner frequency"));
}

void RackCrossoverReadout::setReadout(
	const QString& newCaption,
	const QString& newPrimary,
	const QString& newSecondary)
{
	if (caption == newCaption && primary == newPrimary
		&& secondary == newSecondary)
	{
		return;
	}

	caption = newCaption;
	primary = newPrimary;
	secondary = newSecondary;

	const QString description = secondary.isEmpty()
		? tr("%1: %2").arg(caption, primary)
		: tr("%1: %2, %3").arg(caption, primary, secondary);
	setAccessibleName(caption);
	setAccessibleDescription(description);
	setToolTip(description);
	updateGeometry();
	update();
}

QFont RackCrossoverReadout::captionFont() const
{
	return rackFont(skinTokens, 8, true, 1.5);
}

QFont RackCrossoverReadout::valueFont() const
{
	return rackMonoFont(skinTokens, 12, true, 0.3);
}

QSize RackCrossoverReadout::sizeHint() const
{
	const QFontMetrics captionMetrics(captionFont());
	const QFontMetrics valueMetrics(valueFont());
	const int contentWidth = qMax(
		captionMetrics.horizontalAdvance(caption),
		qMax(valueMetrics.horizontalAdvance(primary),
			valueMetrics.horizontalAdvance(secondary)));
	const int lines = secondary.isEmpty() ? 1 : 2;

	return QSize(
		qMax(GUIHelper::scale(132.0), contentWidth + GUIHelper::scale(20.0)),
		GUIHelper::scale(lines == 1 ? 46.0 : 62.0));
}

QSize RackCrossoverReadout::minimumSizeHint() const
{
	// The value is the instrument: never let the layout squeeze the readout
	// below what its current value needs, or "80 Hz LR4" degrades into
	// "80..." while decorative neighbours keep their width.
	const QFontMetrics valueMetrics(valueFont());
	const int contentWidth = qMax(
		valueMetrics.horizontalAdvance(primary),
		valueMetrics.horizontalAdvance(secondary));
	const int lines = secondary.isEmpty() ? 1 : 2;
	return QSize(
		qMax(GUIHelper::scale(96.0),
			contentWidth + GUIHelper::scale(14.0)),
		GUIHelper::scale(lines == 1 ? 42.0 : 58.0));
}

void RackCrossoverReadout::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	const SkinTokens& tokens = skinTokens;
	const QColor textInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);

	const QFont captionFace = captionFont();
	const QFont valueFace = valueFont();
	const QFontMetrics captionMetrics(captionFace);
	const QFontMetrics valueMetrics(valueFace);
	const qreal horizontalPadding = GUIHelper::scale(6.0);
	const qreal topPadding = GUIHelper::scale(2.0);
	const qreal bottomPadding = GUIHelper::scale(2.0);
	const qreal availableHeight =
		height() - topPadding - bottomPadding;
	const qreal contentWidth =
		qMax<qreal>(0.0, width() - horizontalPadding * 2.0);

	// The engraved caption over the readout lines, nothing else. The old
	// printed scale pointed at nothing (no knob rides it on a read-only
	// card) and a fake legend is not hardware - review rounds 2 and 3
	// removed the scale and folded the two per-type meters into one.
	qreal cursorY = topPadding;
	if (availableHeight >= captionMetrics.height()
		+ valueMetrics.height() + GUIHelper::scale(4.0))
	{
		const QRectF captionRect(
			horizontalPadding,
			cursorY,
			contentWidth,
			captionMetrics.height() + GUIHelper::scale(1.0));
		drawEngravedText(
			painter,
			this,
			captionRect,
			Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
			fittedText(caption, captionFace, captionRect.width()),
			captionFace,
			mutedInk);
		cursorY = captionRect.bottom() + GUIHelper::scale(3.0);
	}

	const QString lines[] = {primary, secondary};
	for (const QString& line : lines)
	{
		if (line.isEmpty())
			continue;
		if (height() - bottomPadding - cursorY
			< valueMetrics.height())
		{
			break;
		}

		const QRectF lineRect(
			horizontalPadding,
			cursorY,
			contentWidth,
			valueMetrics.height());
		drawEngravedText(
			painter,
			this,
			lineRect,
			Qt::AlignHCenter |
				Qt::AlignVCenter |
				Qt::TextSingleLine,
			fittedText(line, valueFace, lineRect.width()),
			valueFace,
			textInk);
		cursorY = lineRect.bottom() + GUIHelper::scale(2.0);
	}
}

// ---- RackLfeLamp ----------------------------------------------------------

RackLfeLamp::RackLfeLamp(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent), skinTokens(tokens)
{
	setObjectName(QStringLiteral("RackBassLfeLamp"));
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setAccessibleName(tr("Source LFE lamp"));
}

void RackLfeLamp::setLfeState(bool newPreserved, double newGainDb)
{
	preserved = newPreserved;
	gainDb = newGainDb;

	QString description;

	if (!preserved)
	{
		description = tr("Source LFE is not preserved");
	}
	else if (std::isfinite(gainDb))
	{
		description = tr("Source LFE is preserved at %1 dB")
			.arg(formattedDb(gainDb));
	}
	else
	{
		description = tr("Source LFE is preserved; gain: --");
	}

	setAccessibleDescription(description);
	setToolTip(description);
	update();
}

QFont RackLfeLamp::captionFont() const
{
	return rackFont(skinTokens, 8, true, 1.4);
}

QFont RackLfeLamp::valueFont() const
{
	return rackMonoFont(skinTokens, 8, true, 0.2);
}

QSize RackLfeLamp::sizeHint() const
{
	return QSize(
		GUIHelper::scale(82.0),
		GUIHelper::scale(62.0));
}

QSize RackLfeLamp::minimumSizeHint() const
{
	return sizeHint();
}

void RackLfeLamp::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const SkinTokens& tokens = skinTokens;
	const QPalette::ColorGroup group = isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor textInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	const QColor lampColor(tokens.accent2);
	const QColor shadow = palette().color(group, QPalette::Shadow);
	const QColor highlight = palette().color(group, QPalette::Light);
	const bool on = preserved && isEnabled();

	const QFont captionFace = captionFont();
	const QFont valueFace = valueFont();
	const QFontMetrics captionMetrics(captionFace);
	const QFontMetrics valueMetrics(valueFace);
	const qreal horizontalPadding = GUIHelper::scale(3.0);
	const qreal topPadding = GUIHelper::scale(2.0);
	const qreal bottomPadding = GUIHelper::scale(2.0);

	const QRectF captionRect(
		horizontalPadding,
		topPadding,
		qMax<qreal>(0.0, width() - horizontalPadding * 2.0),
		captionMetrics.height());
	const QString shownCaption = fittedText(
		tr("LFE"),
		captionFace,
		captionRect.width());

	if (captionRect.height() >= captionMetrics.height())
	{
		drawEngravedText(
			painter,
			this,
			captionRect,
			Qt::AlignHCenter |
				Qt::AlignVCenter |
				Qt::TextSingleLine,
			shownCaption,
			captionFace,
			mutedInk);
	}

	QString valueText;

	if (!preserved)
	{
		valueText = tr("CUT");
	}
	else if (!std::isfinite(gainDb))
	{
		valueText = tr("ON  -- dB");
	}
	else
	{
		valueText = tr("ON  %1 dB").arg(formattedDb(gainDb));
	}

	const QRectF valueRect(
		horizontalPadding,
		qMax<qreal>(
			topPadding,
			height() - bottomPadding - valueMetrics.height()),
		qMax<qreal>(0.0, width() - horizontalPadding * 2.0),
		valueMetrics.height());
	const QString shownValue = fittedText(
		valueText,
		valueFace,
		valueRect.width());

	const qreal lampAreaTop =
		captionRect.bottom() + GUIHelper::scale(2.0);
	const qreal lampAreaBottom =
		valueRect.top() - GUIHelper::scale(2.0);
	const qreal lampAreaHeight =
		qMax<qreal>(0.0, lampAreaBottom - lampAreaTop);
	const qreal lampSize = qMin<qreal>(
		GUIHelper::scale(15.0),
		qMin(
			qMax<qreal>(0.0, width() - GUIHelper::scale(16.0)),
			qMax<qreal>(0.0, lampAreaHeight - GUIHelper::scale(8.0))));

	if (lampSize >= GUIHelper::scale(6.0))
	{
		const QPointF center(
			width() / 2.0,
			(lampAreaTop + lampAreaBottom) / 2.0);
		const QRectF bezel(
			center.x() - lampSize / 2.0 - GUIHelper::scale(2.0),
			center.y() - lampSize / 2.0 - GUIHelper::scale(2.0),
			lampSize + GUIHelper::scale(4.0),
			lampSize + GUIHelper::scale(4.0));
		const QRectF dome = bezel.adjusted(
			GUIHelper::scale(2.5),
			GUIHelper::scale(2.5),
			-GUIHelper::scale(2.5),
			-GUIHelper::scale(2.5));

		painter.setPen(QPen(
			enabledInk(this, shadow, 210),
			physicalPixel(this)));
		painter.setBrush(enabledInk(this, shadow.darker(145), 210));
		painter.drawRoundedRect(bezel, 2.0, 2.0);

		if (on)
		{
			const QRectF halo = bezel.adjusted(
				-GUIHelper::scale(4.0),
				-GUIHelper::scale(4.0),
				GUIHelper::scale(4.0),
				GUIHelper::scale(4.0));
			QRadialGradient haloGradient(
				halo.center(),
				halo.width() / 2.0);
			haloGradient.setColorAt(0.0, withAlpha(lampColor, 100));
			haloGradient.setColorAt(1.0, withAlpha(lampColor, 0));
			painter.setPen(Qt::NoPen);
			painter.setBrush(haloGradient);
			painter.drawRoundedRect(halo, 5.0, 5.0);
		}

		QRadialGradient domeGradient(
			dome.center() -
				QPointF(
					dome.width() * 0.2,
					dome.height() * 0.2),
			dome.width());

		if (on)
		{
			domeGradient.setColorAt(0.0, lampColor.lighter(155));
			domeGradient.setColorAt(1.0, lampColor.darker(135));
		}
		else
		{
			const QColor offColor = lampColor.darker(340);
			domeGradient.setColorAt(0.0, offColor.lighter(135));
			domeGradient.setColorAt(1.0, offColor);
		}

		painter.setPen(Qt::NoPen);
		painter.setBrush(domeGradient);
		painter.drawRoundedRect(dome, 1.5, 1.5);

		const QRectF specular(
			dome.left() + dome.width() * 0.18,
			dome.top() + dome.height() * 0.18,
			dome.width() * 0.28,
			dome.height() * 0.22);
		painter.setBrush(withAlpha(highlight, on ? 165 : 45));
		painter.drawRoundedRect(specular, 1.0, 1.0);
	}

	if (valueRect.height() >= valueMetrics.height())
	{
		drawEngravedText(
			painter,
			this,
			valueRect,
			Qt::AlignHCenter |
				Qt::AlignVCenter |
				Qt::TextSingleLine,
			shownValue,
			valueFace,
			preserved ? textInk : mutedInk);
	}
}

// ---- RackHeadroomMeter ----------------------------------------------------

RackHeadroomMeter::RackHeadroomMeter(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent), skinTokens(tokens)
{
	setObjectName(QStringLiteral("RackBassHeadroomMeter"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(tr("Applied headroom trim"));
}

void RackHeadroomMeter::setHeadroom(bool newAutomatic, double newTrimDb)
{
	automatic = newAutomatic;
	trimDb = newTrimDb;
	trimFinite = std::isfinite(trimDb);

	QString description;

	if (trimFinite)
	{
		description = automatic
			? tr("Automatic headroom trim: %1 dB")
				.arg(formattedDb(trimDb))
			: tr("Manual headroom trim: %1 dB")
				.arg(formattedDb(trimDb));
	}
	else
	{
		description = automatic
			? tr("Automatic headroom trim: --")
			: tr("Manual headroom trim: --");
	}

	setAccessibleDescription(description);
	setToolTip(description);
	update();
}

QFont RackHeadroomMeter::captionFont() const
{
	return rackMonoFont(skinTokens, 9, true, 0.3);
}

QFont RackHeadroomMeter::scaleFont() const
{
	return rackMonoFont(skinTokens, 7, false);
}

QSize RackHeadroomMeter::sizeHint() const
{
	return QSize(
		GUIHelper::scale(220.0),
		GUIHelper::scale(64.0));
}

QSize RackHeadroomMeter::minimumSizeHint() const
{
	return QSize(
		GUIHelper::scale(154.0),
		GUIHelper::scale(60.0));
}

void RackHeadroomMeter::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	const SkinTokens& tokens = skinTokens;
	const QColor textInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	const QColor accentInk(tokens.accent);
	const QColor glassTop(QStringLiteral("#151A17"));
	const QColor glassBottom(QStringLiteral("#080B09"));
	const QColor glassBorder(QStringLiteral("#050807"));

	const QFont captionFace = captionFont();
	const QFont scaleFace = scaleFont();
	const QFontMetrics captionMetrics(captionFace);
	const QFontMetrics scaleMetrics(scaleFace);
	const qreal horizontalPadding = GUIHelper::scale(4.0);
	const qreal topPadding = GUIHelper::scale(1.0);
	const qreal bottomPadding = GUIHelper::scale(2.0);

	QString captionText;

	if (trimFinite)
	{
		captionText = automatic
			? tr("AUTO %1 dB").arg(formattedDb(trimDb))
			: tr("MANUAL %1 dB").arg(formattedDb(trimDb));
	}
	else
	{
		captionText = automatic
			? tr("AUTO --")
			: tr("MANUAL --");
	}

	const QRectF captionRect(
		horizontalPadding,
		topPadding,
		qMax<qreal>(0.0, width() - horizontalPadding * 2.0),
		captionMetrics.height() + GUIHelper::scale(1.0));
	const QString shownCaption = fittedText(
		captionText,
		captionFace,
		captionRect.width());

	if (captionRect.height() >= captionMetrics.height())
	{
		drawEngravedText(
			painter,
			this,
			captionRect,
			Qt::AlignLeft |
				Qt::AlignVCenter |
				Qt::TextSingleLine,
			shownCaption,
			captionFace,
			trimFinite ? textInk : mutedInk);
	}

	static const int tickValues[] = { -24, -18, -12, -6, 0 };
	const QString tickTexts[] = {
		tr("-24"),
		tr("-18"),
		tr("-12"),
		tr("-6"),
		tr("0")
	};

	const qreal leftLabelHalf =
		(scaleMetrics.horizontalAdvance(tickTexts[0]) +
			GUIHelper::scale(4.0)) / 2.0;
	const qreal rightLabelHalf =
		(scaleMetrics.horizontalAdvance(tickTexts[4]) +
			GUIHelper::scale(4.0)) / 2.0;
	const qreal left = qMax<qreal>(
		GUIHelper::scale(9.0),
		leftLabelHalf + GUIHelper::scale(1.0));
	const qreal right = qMin<qreal>(
		width() - GUIHelper::scale(9.0),
		width() - rightLabelHalf - GUIHelper::scale(1.0));
	const qreal trackTop =
		captionRect.bottom() + GUIHelper::scale(6.0);
	const qreal trackHeight = GUIHelper::scale(8.0);
	const qreal trackBottom = trackTop + trackHeight;

	if (right - left < GUIHelper::scale(12.0) ||
		trackBottom > height() - bottomPadding)
	{
		return;
	}

	const QRectF track(
		left,
		trackTop,
		right - left,
		trackHeight);

	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient trackGradient(track.topLeft(), track.bottomLeft());
	trackGradient.setColorAt(0.0, enabledInk(this, glassTop, 245));
	trackGradient.setColorAt(1.0, enabledInk(this, glassBottom, 245));
	painter.setPen(QPen(
		enabledInk(this, glassBorder, 245),
		physicalPixel(this)));
	painter.setBrush(trackGradient);
	painter.drawRoundedRect(track, 1.5, 1.5);

	qreal markerX = left;

	if (trimFinite)
	{
		const double boundedTrim = std::clamp(trimDb, -24.0, 0.0);
		const qreal fraction = qreal((boundedTrim + 24.0) / 24.0);
		markerX = left + fraction * (right - left);

		QRectF fill = track.adjusted(
			physicalPixel(this),
			physicalPixel(this),
			-physicalPixel(this),
			-physicalPixel(this));
		fill.setRight(qMax(fill.left(), markerX));

		if (fill.width() > physicalPixel(this))
		{
			QLinearGradient fillGradient(
				fill.topLeft(),
				fill.topRight());
			fillGradient.setColorAt(
				0.0,
				enabledInk(this, accentInk.darker(150), 190));
			fillGradient.setColorAt(
				1.0,
				enabledInk(this, accentInk, 235));
			painter.setPen(Qt::NoPen);
			painter.setBrush(fillGradient);
			painter.drawRoundedRect(fill, 1.0, 1.0);
		}
	}

	painter.setRenderHint(QPainter::Antialiasing, false);

	for (int index = 0; index < 5; ++index)
	{
		const qreal fraction = qreal(tickValues[index] + 24) / 24.0;
		const qreal x = left + fraction * (right - left);
		const bool stop = index == 0 || index == 4;

		drawCrispVerticalLine(
			painter,
			this,
			x,
			trackBottom + GUIHelper::scale(2.0),
			trackBottom + GUIHelper::scale(stop ? 8.0 : 6.0),
			enabledInk(
				this,
				stop ? textInk : mutedInk,
				190));
	}

	if (trimFinite)
	{
		drawCrispVerticalLine(
			painter,
			this,
			markerX,
			trackTop - GUIHelper::scale(4.0),
			trackBottom + GUIHelper::scale(2.0),
			enabledInk(this, accentInk),
			2.0);
	}

	const qreal labelTop =
		trackBottom + GUIHelper::scale(8.0);

	if (labelTop + scaleMetrics.height() >
		height() - bottomPadding)
	{
		return;
	}

	auto labelRect = [&](int index)
	{
		const qreal fraction = qreal(tickValues[index] + 24) / 24.0;
		const qreal x = left + fraction * (right - left);
		const qreal labelWidth =
			scaleMetrics.horizontalAdvance(tickTexts[index]) +
			GUIHelper::scale(4.0);
		const qreal labelLeft = std::clamp(
			x - labelWidth / 2.0,
			qreal(0.0),
			qMax<qreal>(0.0, width() - labelWidth));

		return QRectF(
			labelLeft,
			labelTop,
			labelWidth,
			scaleMetrics.height());
	};

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setFont(scaleFace);
	painter.setPen(enabledInk(this, mutedInk, 220));

	const QRectF firstRect = labelRect(0);
	const QRectF lastRect = labelRect(4);
	painter.drawText(
		firstRect,
		Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
		tickTexts[0]);

	qreal occupiedRight = firstRect.right();

	for (int index = 1; index < 4; ++index)
	{
		const QRectF currentRect = labelRect(index);

		if (currentRect.left() >=
				occupiedRight + GUIHelper::scale(2.0) &&
			currentRect.right() <=
				lastRect.left() - GUIHelper::scale(2.0))
		{
			painter.drawText(
				currentRect,
				Qt::AlignHCenter |
					Qt::AlignTop |
					Qt::TextSingleLine,
				tickTexts[index]);
			occupiedRight = currentRect.right();
		}
	}

	if (lastRect.left() >=
		occupiedRight + GUIHelper::scale(2.0))
	{
		painter.drawText(
			lastRect,
			Qt::AlignHCenter |
				Qt::AlignTop |
				Qt::TextSingleLine,
			tickTexts[4]);
	}
}

// ---- RackSubwooferRoutingCardView ------------------------------------------
