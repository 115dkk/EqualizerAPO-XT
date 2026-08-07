/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
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
#include "Editor/skins/SkinPaint.h"

class RackElidingLabel final : public QLabel
{
public:
	explicit RackElidingLabel(QWidget* parent = nullptr)
		: QLabel(parent)
	{
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	}

	void setFullText(const QString& text)
	{
		if (fullText == text)
			return;

		fullText = text;
		updateDisplayedText();
	}

	QSize minimumSizeHint() const override
	{
		QSize result = QLabel::minimumSizeHint();
		result.setWidth(0);
		return result;
	}

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		updateDisplayedText();
	}

	bool event(QEvent* event) override
	{
		const QEvent::Type type = event->type();
		const bool refresh =
			type == QEvent::FontChange ||
			type == QEvent::StyleChange ||
			type == QEvent::Polish ||
			type == QEvent::ApplicationFontChange;

		const bool result = QLabel::event(event);

		if (refresh)
			updateDisplayedText();

		return result;
	}

private:
	void updateDisplayedText()
	{
		const int availableWidth = qMax(
			0,
			width() - contentsMargins().left() - contentsMargins().right());
		const QString displayed = QFontMetrics(font()).elidedText(
			fullText,
			Qt::ElideRight,
			availableWidth);

		if (QLabel::text() != displayed)
			QLabel::setText(displayed);
	}

	QString fullText;
};

namespace
{
qreal physicalPixel(const QWidget* widget)
{
	return 1.0 / qMax<qreal>(1.0, widget->devicePixelRatioF());
}

qreal crispCoordinate(const QWidget* widget, qreal value)
{
	const qreal ratio = qMax<qreal>(1.0, widget->devicePixelRatioF());
	return (qFloor(value * ratio) + 0.5) / ratio;
}

QColor enabledInk(const QWidget* widget, const QColor& color, int enabledAlpha = 255)
{
	return withAlpha(
		color,
		widget->isEnabled() ? enabledAlpha : qMin(enabledAlpha, 90));
}

QFont rackFont(int pixelSize, bool bold, qreal letterSpacing = 0.0)
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QFont font(tokens.fontFamily);
	font.setPixelSize(pixelSize);
	font.setBold(bold);

	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);

	return font;
}

QFont rackMonoFont(int pixelSize, bool bold, qreal letterSpacing = 0.0)
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QFont font(tokens.monoFontFamily);

	if (tokens.monoFontFamily.isEmpty())
		font.setStyleHint(QFont::Monospace);

	font.setPixelSize(pixelSize);
	font.setBold(bold);

	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);

	return font;
}

QString fittedText(
	const QString& text,
	const QFont& font,
	qreal availableWidth)
{
	if (availableWidth <= 0.0)
		return QString();

	return QFontMetrics(font).elidedText(
		text,
		Qt::ElideRight,
		qMax(0, qFloor(availableWidth)));
}

QString formattedDb(double value)
{
	if (!std::isfinite(value))
		return QStringLiteral("--");

	QString text = QString::number(value, 'f', 1);

	if (value > 0.0)
		text.prepend(QLatin1Char('+'));

	return text;
}

void drawEngravedText(
	QPainter& painter,
	const QWidget* widget,
	const QRectF& rect,
	int flags,
	const QString& text,
	const QFont& font,
	const QColor& ink)
{
	if (text.isEmpty() || rect.width() <= 0.0 || rect.height() <= 0.0)
		return;

	const QPalette::ColorGroup group = widget->isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor recess = widget->palette().color(group, QPalette::Shadow);

	painter.setFont(font);
	painter.setPen(enabledInk(widget, recess, 150));
	painter.drawText(
		rect.translated(0.0, physicalPixel(widget)),
		flags,
		text);
	painter.setPen(enabledInk(widget, ink));
	painter.drawText(rect, flags, text);
}

void drawCrispHorizontalLine(
	QPainter& painter,
	const QWidget* widget,
	qreal left,
	qreal right,
	qreal y,
	const QColor& color)
{
	if (right <= left)
		return;

	painter.setPen(QPen(color, physicalPixel(widget)));
	painter.drawLine(
		QPointF(
			crispCoordinate(widget, left),
			crispCoordinate(widget, y)),
		QPointF(
			crispCoordinate(widget, right),
			crispCoordinate(widget, y)));
}

void drawCrispVerticalLine(
	QPainter& painter,
	const QWidget* widget,
	qreal x,
	qreal top,
	qreal bottom,
	const QColor& color,
	qreal width = 1.0)
{
	if (bottom <= top)
		return;

	painter.setPen(QPen(color, width * physicalPixel(widget)));
	painter.drawLine(
		QPointF(
			crispCoordinate(widget, x),
			crispCoordinate(widget, top)),
		QPointF(
			crispCoordinate(widget, x),
			crispCoordinate(widget, bottom)));
}

void setSeverityProperty(QWidget* widget, const QString& severity)
{
	if (widget == nullptr || widget->property("severity").toString() == severity)
		return;

	widget->setProperty("severity", severity);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

bool containsLabelCharacters(const QString& text)
{
	for (const QChar character : text)
	{
		if (character.isLetterOrNumber())
			return true;
	}

	return false;
}
}

// ---- RackCrossoverReadout -------------------------------------------------

RackCrossoverReadout::RackCrossoverReadout(QWidget* parent)
	: QWidget(parent)
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
	return rackFont(8, true, 1.5);
}

QFont RackCrossoverReadout::valueFont() const
{
	return rackMonoFont(12, true, 0.3);
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
	const SkinTokens& tokens = SkinManager::instance()->tokens();
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

RackLfeLamp::RackLfeLamp(QWidget* parent)
	: QWidget(parent)
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
	return rackFont(8, true, 1.4);
}

QFont RackLfeLamp::valueFont() const
{
	return rackMonoFont(8, true, 0.2);
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

	const SkinTokens& tokens = SkinManager::instance()->tokens();
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

RackHeadroomMeter::RackHeadroomMeter(QWidget* parent)
	: QWidget(parent)
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
	return rackMonoFont(9, true, 0.3);
}

QFont RackHeadroomMeter::scaleFont() const
{
	return rackMonoFont(7, false);
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
	const SkinTokens& tokens = SkinManager::instance()->tokens();
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

RackSubwooferRoutingCardView::RackSubwooferRoutingCardView(QWidget* parent)
	: SubwooferRoutingCardView(parent)
{
	setObjectName(QStringLiteral("RackSubwooferRoutingCardView"));
	setAutoFillBackground(false);

	const int sideMargin = GUIHelper::scale(28.0);
	const int topMargin = GUIHelper::scale(12.0);
	const int bottomMargin = GUIHelper::scale(12.0);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(
		sideMargin,
		topMargin,
		sideMargin,
		bottomMargin);
	root->setSpacing(GUIHelper::scale(8.0));

	headerWidget = new QWidget(this);
	headerWidget->setObjectName(QStringLiteral("RackBassHeader"));

	QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(GUIHelper::scale(8.0));

	validityLabel = new QLabel(headerWidget);
	validityLabel->setObjectName(QStringLiteral("RackBassValidity"));
	validityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	validityLabel->setAccessibleName(tr("Bass-management validity"));
	headerLayout->addWidget(validityLabel, 0, Qt::AlignVCenter);

	layoutLabel = new RackElidingLabel(headerWidget);
	layoutLabel->setObjectName(QStringLiteral("RackBassLayout"));
	layoutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	headerLayout->addWidget(layoutLabel, 1, Qt::AlignVCenter);

	profileLabel = new RackElidingLabel(headerWidget);
	profileLabel->setObjectName(QStringLiteral("RackBassProfile"));
	profileLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	profileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	profileLabel->setMaximumWidth(GUIHelper::scale(280.0));
	profileLabel->setAccessibleName(tr("Bass-management profile"));
	headerLayout->addWidget(profileLabel, 0, Qt::AlignVCenter);

	root->addWidget(headerWidget);

	instrumentWidget = new QWidget(this);
	instrumentWidget->setObjectName(QStringLiteral("RackBassInstrumentRow"));

	QHBoxLayout* instrumentLayout = new QHBoxLayout(instrumentWidget);
	instrumentLayout->setContentsMargins(0, 0, 0, 0);
	instrumentLayout->setSpacing(GUIHelper::scale(8.0));

	crossoverReadout = new RackCrossoverReadout(instrumentWidget);
	instrumentLayout->addWidget(
		crossoverReadout,
		1,
		Qt::AlignVCenter);

	lfeLamp = new RackLfeLamp(instrumentWidget);
	instrumentLayout->addWidget(
		lfeLamp,
		0,
		Qt::AlignVCenter);

	headroomMeter = new RackHeadroomMeter(instrumentWidget);
	instrumentLayout->addWidget(
		headroomMeter,
		2,
		Qt::AlignVCenter);

	actionHost = new QWidget(instrumentWidget);
	actionHost->setObjectName(QStringLiteral("RackBassActionHost"));
	actionHost->setAccessibleName(tr("Bass-management actions"));

	actionLayout = new QHBoxLayout(actionHost);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(GUIHelper::scale(8.0));

	actionHost->setVisible(false);
	instrumentLayout->addWidget(
		actionHost,
		0,
		Qt::AlignVCenter);

	root->addWidget(instrumentWidget);

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(QStringLiteral("RackBassStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	statusLabel->setAccessibleName(tr("Bass-management status"));
	statusLabel->setVisible(false);
	root->addWidget(statusLabel);

	connect(
		SkinManager::instance(),
		&SkinManager::skinChanged,
		this,
		[this]()
		{
			layoutLabel->update();
			profileLabel->update();
			validityLabel->update();
			statusLabel->update();
			crossoverReadout->updateGeometry();
			lfeLamp->updateGeometry();
			headroomMeter->updateGeometry();
			crossoverReadout->update();
			lfeLamp->update();
			headroomMeter->update();
			headerWidget->update();
			instrumentWidget->update();
			updateGeometry();
			updateResponsiveLayout();
			update();
		});

	setSeverityProperty(validityLabel, QStringLiteral("valid"));
	setSeverityProperty(profileLabel, QStringLiteral("normal"));
	setSeverityProperty(statusLabel, QStringLiteral("warning"));
	updateResponsiveLayout();
}

void RackSubwooferRoutingCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	const int actionIndex = actionLayout->count();

	button->setParent(actionHost);
	button->setObjectName(QStringLiteral("RackBassActionButton"));
	button->setFocusPolicy(Qt::StrongFocus);
	button->setProperty("rackBassAction", true);

	QString buttonText = button->text();
	QString plainButtonText = buttonText;
	plainButtonText.remove(QLatin1Char('&'));

	if (!containsLabelCharacters(plainButtonText))
	{
		if (actionIndex == 0)
		{
			buttonText = tr("Open editor");
		}
		else if (actionIndex == 1)
		{
			buttonText = tr("Preset");
		}
		else if (!button->accessibleName().trimmed().isEmpty())
		{
			buttonText = button->accessibleName();
		}
		else if (!button->toolTip().trimmed().isEmpty())
		{
			buttonText = button->toolTip();
		}
		else
		{
			buttonText = tr("Action");
		}

		button->setText(buttonText);
	}

	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
	{
		toolButton->setAutoRaise(false);
		toolButton->setToolButtonStyle(
			toolButton->icon().isNull()
				? Qt::ToolButtonTextOnly
				: Qt::ToolButtonTextBesideIcon);
	}

	QString actionName = button->accessibleName().trimmed();

	if (actionName.isEmpty())
	{
		actionName = button->text();
		actionName.remove(QLatin1Char('&'));

		if (actionName.trimmed().isEmpty())
			actionName = tr("Bass-management action");

		button->setAccessibleName(actionName);
	}

	if (button->toolTip().trimmed().isEmpty())
		button->setToolTip(actionName);

	button->style()->unpolish(button);
	button->style()->polish(button);
	button->ensurePolished();

	const int targetHeight = GUIHelper::scale(40.0);
	const int targetWidth = qMax(
		GUIHelper::scale(40.0),
		button->sizeHint().width());
	button->setMinimumSize(
		qMax(button->minimumWidth(), targetWidth),
		qMax(button->minimumHeight(), targetHeight));

	actionLayout->addWidget(button);
	actionHost->setVisible(true);
	updateResponsiveLayout();
	updateGeometry();
}

void RackSubwooferRoutingCardView::applyState(
	const SubwooferRoutingCardState& state)
{
	// The contract guarantees a missing linked profile is already described
	// by errorText, so the flag no longer feeds a second warning.
	const bool invalid =
		!state.valid ||
		!state.errorText.trimmed().isEmpty();
	const bool hasWarning =
		!state.warningText.trimmed().isEmpty();

	QString validityText;
	QString validityDescription;
	QString validitySeverity;

	if (invalid)
	{
		validityText = tr("X ERROR");
		validityDescription =
			tr("Bass-management state has an error");
		validitySeverity = QStringLiteral("error");
	}
	else if (hasWarning)
	{
		validityText = tr("! WARNING");
		validityDescription =
			tr("Bass-management state has a warning");
		validitySeverity = QStringLiteral("warning");
	}
	else
	{
		validityText = tr("OK READY");
		validityDescription =
			tr("Bass-management state is valid");
		validitySeverity = QStringLiteral("valid");
	}

	validityLabel->setText(validityText);
	validityLabel->setAccessibleDescription(validityDescription);
	validityLabel->setToolTip(validityDescription);
	setSeverityProperty(validityLabel, validitySeverity);

	const QString layoutText =
		state.layoutLabel.trimmed().isEmpty()
			? tr("Unknown layout")
			: state.layoutLabel;
	const QString fullLayoutText =
		tr("LAYOUT  %1").arg(layoutText);
	const QString layoutDescription =
		tr("Physical speaker layout: %1").arg(layoutText);

	layoutLabel->setFullText(fullLayoutText);
	layoutLabel->setAccessibleDescription(layoutDescription);
	layoutLabel->setToolTip(layoutDescription);

	// One crossover instrument: HP and LP lines with their recognized
	// alignment labels; a full-range state engraves exactly that.
	QString highPassLine;
	if (state.highPassHz > 0.0)
	{
		highPassLine = state.highPassSlope.isEmpty()
			? tr("HP %1").arg(formatHz(state.highPassHz))
			: tr("HP %1 %2").arg(formatHz(state.highPassHz),
				state.highPassSlope);
	}
	QString lowPassLine;
	if (state.lowPassHz > 0.0)
	{
		lowPassLine = state.lowPassSlope.isEmpty()
			? tr("LP %1").arg(formatHz(state.lowPassHz))
			: tr("LP %1 %2").arg(formatHz(state.lowPassHz),
				state.lowPassSlope);
	}

	if (highPassLine.isEmpty() && lowPassLine.isEmpty())
	{
		crossoverReadout->setReadout(
			tr("CROSSOVER"), tr("FULL RANGE"));
	}
	else if (highPassLine.isEmpty() || lowPassLine.isEmpty())
	{
		crossoverReadout->setReadout(
			tr("CROSSOVER"),
			highPassLine.isEmpty() ? lowPassLine : highPassLine);
	}
	else
	{
		crossoverReadout->setReadout(
			tr("CROSSOVER"), highPassLine, lowPassLine);
	}

	lfeLamp->setLfeState(
		state.sourceLfePreserved,
		state.sourceLfeGainDb);
	headroomMeter->setHeadroom(
		state.headroomAuto,
		state.headroomTrimDb);

	QString profileText;
	QString profileDescription;
	QString profileSeverity = QStringLiteral("normal");

	if (state.linkedProfile)
	{
		const QString name = state.profileName.trimmed().isEmpty()
			? tr("Unnamed profile")
			: state.profileName;

		// The nameplate stays a data readout; when the file is missing the
		// status line below already posts the cause, so only the ink
		// (severity) changes here.
		profileText = tr("LINKED  %1").arg(name);
		if (state.profileMissing)
		{
			profileDescription =
				tr("Linked profile is missing: %1").arg(name);
			profileSeverity = QStringLiteral("warning");
		}
		else
		{
			profileDescription =
				tr("Linked subwoofer-routing profile: %1").arg(name);
		}
	}
	else
	{
		const QString name = state.profileName.trimmed().isEmpty()
			? tr("Embedded state")
			: state.profileName;
		profileText = tr("LOCAL  %1").arg(name);
		profileDescription =
			tr("Embedded subwoofer-routing state: %1").arg(name);
	}

	profileLabel->setFullText(profileText);
	profileLabel->setAccessibleDescription(profileDescription);
	profileLabel->setToolTip(profileDescription);
	setSeverityProperty(profileLabel, profileSeverity);

	// One status line, highest severity first - the panel posts a fault
	// once (status contract, review round 2). The validity lamp names the
	// state; this line carries the cause.
	QString statusText;
	if (!state.errorText.trimmed().isEmpty())
	{
		statusText = tr("ERROR: %1").arg(state.errorText);
	}
	else if (invalid)
	{
		statusText = tr("ERROR: Invalid subwoofer-routing state");
	}
	else if (hasWarning)
	{
		statusText = tr("WARNING: %1").arg(state.warningText);
	}

	statusLabel->setText(statusText);
	statusLabel->setAccessibleDescription(statusText);
	statusLabel->setToolTip(statusText);
	statusLabel->setVisible(!statusText.isEmpty());
	setSeverityProperty(
		statusLabel,
		invalid
			? QStringLiteral("error")
			: QStringLiteral("warning"));

	updateResponsiveLayout();
	updateGeometry();
	update();
}

void RackSubwooferRoutingCardView::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(tokens);
	const QPalette::ColorGroup group = isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor panel = palette().color(group, QPalette::Button);
	const QColor shadow = palette().color(group, QPalette::Shadow);
	const QColor highlight = palette().color(group, QPalette::Light);
	const QColor border(tokens.mutedText);
	const qreal inset = 0.5 * physicalPixel(this);
	const QRectF face = QRectF(rect()).adjusted(
		inset,
		inset,
		-inset,
		-inset);

	QLinearGradient faceGradient(face.topLeft(), face.bottomLeft());
	faceGradient.setColorAt(
		0.0,
		dark ? panel.lighter(112) : panel.lighter(103));
	faceGradient.setColorAt(0.48, panel);
	faceGradient.setColorAt(
		1.0,
		dark ? panel.darker(112) : panel.darker(106));

	painter.setPen(QPen(
		enabledInk(this, border, 145),
		physicalPixel(this)));
	painter.setBrush(faceGradient);
	painter.drawRoundedRect(face, 3.0, 3.0);

	painter.setRenderHint(QPainter::Antialiasing, false);

	for (int y = 3; y < height() - 3; y += 3)
	{
		const int sequence = (y * 17 + height() * 5) % 11;
		const QColor grain = sequence < 3
			? enabledInk(this, highlight, 12 + sequence * 3)
			: enabledInk(this, shadow, 7 + sequence);

		drawCrispHorizontalLine(
			painter,
			this,
			3.0,
			width() - 3.0,
			y,
			grain);
	}

	drawCrispHorizontalLine(
		painter,
		this,
		4.0,
		width() - 4.0,
		4.0,
		enabledInk(this, highlight, 105));
	drawCrispHorizontalLine(
		painter,
		this,
		4.0,
		width() - 4.0,
		height() - 5.0,
		enabledInk(this, shadow, 145));

	painter.setRenderHint(QPainter::Antialiasing, true);

	const qreal screwRadius = GUIHelper::scale(3.6);
	const qreal screwInset = GUIHelper::scale(10.0);
	const QPointF screwCenters[] = {
		QPointF(screwInset, screwInset),
		QPointF(width() - screwInset, screwInset),
		QPointF(screwInset, height() - screwInset),
		QPointF(width() - screwInset, height() - screwInset)
	};
	const qreal slotAngles[] = {
		-0.25,
		0.42,
		0.18,
		-0.48
	};

	for (int index = 0; index < 4; ++index)
	{
		const QPointF center = screwCenters[index];
		QRadialGradient screwGradient(
			center -
				QPointF(
					screwRadius * 0.35,
					screwRadius * 0.35),
			screwRadius * 1.7);
		screwGradient.setColorAt(0.0, highlight);
		screwGradient.setColorAt(
			0.55,
			panel.lighter(dark ? 125 : 108));
		screwGradient.setColorAt(1.0, shadow);

		painter.setPen(QPen(
			enabledInk(this, shadow, 185),
			physicalPixel(this)));
		painter.setBrush(screwGradient);
		painter.drawEllipse(
			center,
			screwRadius,
			screwRadius);

		const qreal dx =
			std::cos(slotAngles[index]) *
			screwRadius *
			0.62;
		const qreal dy =
			std::sin(slotAngles[index]) *
			screwRadius *
			0.62;

		painter.setPen(QPen(
			enabledInk(this, shadow, 215),
			physicalPixel(this)));
		painter.drawLine(
			center - QPointF(dx, dy),
			center + QPointF(dx, dy));
	}

	const QString railText = QStringLiteral("SUBWOOFER ROUTING");
	const QFont railFace = rackFont(7, true, 1.4);
	const QFontMetrics railMetrics(railFace);
	const qreal railTop =
		screwInset + screwRadius + GUIHelper::scale(5.0);
	const qreal railBottom =
		height() - screwInset - screwRadius - GUIHelper::scale(5.0);
	const qreal railHeight =
		qMax<qreal>(0.0, railBottom - railTop);
	const qreal railWidth = GUIHelper::scale(20.0);
	const QRect tightBounds =
		railMetrics.tightBoundingRect(railText);
	const qreal requiredTextLength = qMax(
		qreal(railMetrics.horizontalAdvance(railText)),
		qreal(tightBounds.width())) +
		GUIHelper::scale(4.0);
	const qreal requiredTextThickness =
		qMax(
			qreal(railMetrics.height()),
			qreal(tightBounds.height())) +
		GUIHelper::scale(2.0);

	if (requiredTextLength <= railHeight &&
		requiredTextThickness <= railWidth)
	{
		painter.save();
		painter.translate(
			GUIHelper::scale(3.0),
			railBottom);
		painter.rotate(-90.0);

		const QRectF railTextRect(
			GUIHelper::scale(2.0),
			0.0,
			railHeight - GUIHelper::scale(4.0),
			railWidth);
		drawEngravedText(
			painter,
			this,
			railTextRect,
			Qt::AlignHCenter |
				Qt::AlignVCenter |
				Qt::TextSingleLine,
			railText,
			railFace,
			QColor(tokens.mutedText));
		painter.restore();
	}
}

void RackSubwooferRoutingCardView::resizeEvent(
	QResizeEvent* event)
{
	SubwooferRoutingCardView::resizeEvent(event);
	updateResponsiveLayout();
}

void RackSubwooferRoutingCardView::updateResponsiveLayout()
{
	const int availableWidth = width();
	const bool showProfile =
		availableWidth >= GUIHelper::scale(650.0);
	const bool showLfe =
		availableWidth >= GUIHelper::scale(760.0);
	const bool showCrossover =
		availableWidth >= GUIHelper::scale(500.0);
	const bool compactActions =
		availableWidth < GUIHelper::scale(700.0);
	const bool hasActions =
		actionLayout->count() > 0;

	profileLabel->setVisible(showProfile);
	lfeLamp->setVisible(showLfe);
	crossoverReadout->setVisible(showCrossover);
	headroomMeter->setVisible(true);
	actionHost->setVisible(hasActions);

	actionLayout->setDirection(
		compactActions
			? QBoxLayout::TopToBottom
			: QBoxLayout::LeftToRight);

	validityLabel->setVisible(true);
	layoutLabel->setVisible(true);
}
