/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#include "RackBassManagementCardView.h"

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QFontMetrics>
#include <QGradient>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStringList>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/SkinPaint.h"

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
	return withAlpha(color, widget->isEnabled() ? enabledAlpha : qMin(enabledAlpha, 90));
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

void drawEngravedText(
	QPainter& painter,
	const QWidget* widget,
	const QRectF& rect,
	int flags,
	const QString& text,
	const QFont& font,
	const QColor& ink)
{
	if (text.isEmpty())
		return;

	const QPalette::ColorGroup group = widget->isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor recess = widget->palette().color(group, QPalette::Shadow);

	painter.setFont(font);
	painter.setPen(enabledInk(widget, recess, 150));
	painter.drawText(rect.translated(0.0, physicalPixel(widget)), flags, text);
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
	painter.setPen(QPen(color, physicalPixel(widget)));
	painter.drawLine(
		QPointF(crispCoordinate(widget, left), crispCoordinate(widget, y)),
		QPointF(crispCoordinate(widget, right), crispCoordinate(widget, y)));
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
	painter.setPen(QPen(color, width * physicalPixel(widget)));
	painter.drawLine(
		QPointF(crispCoordinate(widget, x), crispCoordinate(widget, top)),
		QPointF(crispCoordinate(widget, x), crispCoordinate(widget, bottom)));
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
	const QString& newValue)
{
	if (caption == newCaption && value == newValue)
		return;

	caption = newCaption;
	value = newValue;
	setAccessibleName(tr("%1 crossover").arg(caption));
	setAccessibleDescription(
		tr("%1 corner frequency: %2").arg(caption, value));
	setToolTip(tr("%1 corner frequency: %2").arg(caption, value));
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
	const int valueWidth = QFontMetrics(valueFont()).horizontalAdvance(value);
	return QSize(qMax(GUIHelper::scale(112.0), valueWidth + 20),
		GUIHelper::scale(58.0));
}

QSize RackCrossoverReadout::minimumSizeHint() const
{
	return QSize(GUIHelper::scale(82.0), GUIHelper::scale(54.0));
}

void RackCrossoverReadout::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QPalette::ColorGroup group = isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor textInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	const QColor accentInk(tokens.accent);
	const QColor shadow = palette().color(group, QPalette::Shadow);
	const QColor highlight = palette().color(group, QPalette::Light);

	const QRectF bounds(rect());
	const QRectF captionRect = bounds.adjusted(6, 1, -6, -36);
	drawEngravedText(
		painter,
		this,
		captionRect,
		Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
		caption,
		captionFont(),
		mutedInk);

	painter.setRenderHint(QPainter::Antialiasing, false);
	const qreal legendTop = 19.0;
	const qreal legendBottom = 28.0;
	const qreal left = 8.0;
	const qreal right = qMax(left, width() - 8.0);

	drawCrispHorizontalLine(
		painter,
		this,
		left,
		right,
		legendBottom,
		enabledInk(this, shadow, 170));
	drawCrispHorizontalLine(
		painter,
		this,
		left,
		right,
		legendBottom + physicalPixel(this),
		enabledInk(this, highlight, 110));

	for (int index = 0; index < 7; ++index)
	{
		const qreal fraction = qreal(index) / 6.0;
		const qreal x = left + fraction * (right - left);
		const bool stop = index == 0 || index == 6;
		const bool detent = index == 3;
		const qreal tickTop = detent
			? legendTop - 2.0
			: stop
			? legendTop
			: legendTop + 3.0;
		const QColor tickInk = detent
			? accentInk
			: stop
			? textInk
			: mutedInk;
		drawCrispVerticalLine(
			painter,
			this,
			x,
			tickTop,
			legendBottom,
			enabledInk(this, tickInk, detent ? 230 : 180),
			detent ? 2.0 : 1.0);
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const QRectF valueRect = bounds.adjusted(5, 29, -5, -2);
	const QFont font = valueFont();
	const QString shown = QFontMetrics(font).elidedText(
		value,
		Qt::ElideMiddle,
		qMax(0, int(valueRect.width())));
	drawEngravedText(
		painter,
		this,
		valueRect,
		Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
		shown,
		font,
		textInk);
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
			.arg(QString::number(gainDb, 'f', 1));
	}
	else
	{
		description = tr("Source LFE is preserved; gain is unavailable");
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
	return QSize(GUIHelper::scale(76.0), GUIHelper::scale(58.0));
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

	drawEngravedText(
		painter,
		this,
		QRectF(0, 0, width(), 15),
		Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
		tr("LFE"),
		captionFont(),
		mutedInk);

	const qreal lampSize = GUIHelper::scale(15.0);
	const QPointF center(width() / 2.0, 27.0);
	const QRectF bezel(
		center.x() - lampSize / 2.0 - 2.0,
		center.y() - lampSize / 2.0 - 2.0,
		lampSize + 4.0,
		lampSize + 4.0);
	const QRectF dome = bezel.adjusted(2.5, 2.5, -2.5, -2.5);

	painter.setPen(QPen(enabledInk(this, shadow, 210), physicalPixel(this)));
	painter.setBrush(enabledInk(this, shadow.darker(145), 210));
	painter.drawRoundedRect(bezel, 2.0, 2.0);

	if (on)
	{
		const QRectF halo = bezel.adjusted(-4.0, -4.0, 4.0, 4.0);
		QRadialGradient haloGradient(halo.center(), halo.width() / 2.0);
		haloGradient.setColorAt(0.0, withAlpha(lampColor, 100));
		haloGradient.setColorAt(1.0, withAlpha(lampColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(haloGradient);
		painter.drawRoundedRect(halo, 5.0, 5.0);
	}

	QRadialGradient domeGradient(
		dome.center() - QPointF(dome.width() * 0.2, dome.height() * 0.2),
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

	QString valueText;
	if (!preserved)
	{
		valueText = tr("CUT");
	}
	else if (!std::isfinite(gainDb))
	{
		valueText = tr("ON");
	}
	else
	{
		QString gainText = QString::number(gainDb, 'f', 1);
		if (gainDb > 0.0)
			gainText.prepend(QLatin1Char('+'));
		valueText = tr("ON  %1 dB").arg(gainText);
	}

	drawEngravedText(
		painter,
		this,
		QRectF(2, 40, width() - 4, height() - 40),
		Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
		valueText,
		valueFont(),
		preserved ? textInk : mutedInk);
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
	trimAvailable = std::isfinite(trimDb);

	QString description;
	if (!trimAvailable)
	{
		description = automatic
			? tr("Automatic headroom trim is unavailable")
			: tr("Manual headroom trim is unavailable");
	}
	else
	{
		description = automatic
			? tr("Automatic headroom trim: %1 dB")
				.arg(QString::number(trimDb, 'f', 1))
			: tr("Manual headroom trim: %1 dB")
				.arg(QString::number(trimDb, 'f', 1));
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
	return QSize(GUIHelper::scale(210.0), GUIHelper::scale(58.0));
}

QSize RackHeadroomMeter::minimumSizeHint() const
{
	return QSize(GUIHelper::scale(154.0), GUIHelper::scale(58.0));
}

void RackHeadroomMeter::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);

	QPainter painter(this);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QPalette::ColorGroup group = isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor textInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	const QColor accentInk(tokens.accent);
	const QColor shadow = palette().color(group, QPalette::Shadow);
	const QColor highlight = palette().color(group, QPalette::Light);

	QString captionText;
	if (!trimAvailable)
	{
		captionText = automatic
			? tr("AUTO TRIM  unavailable")
			: tr("MANUAL TRIM  unavailable");
	}
	else
	{
		captionText = automatic
			? tr("AUTO TRIM  %1 dB").arg(QString::number(trimDb, 'f', 1))
			: tr("MANUAL TRIM  %1 dB").arg(QString::number(trimDb, 'f', 1));
	}

	drawEngravedText(
		painter,
		this,
		QRectF(4, 0, width() - 8, 17),
		Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
		captionText,
		captionFont(),
		trimAvailable ? textInk : mutedInk);

	const qreal left = 9.0;
	const qreal right = qMax(left, width() - 9.0);
	const qreal trackTop = 24.0;
	const qreal trackBottom = 31.0;
	const QRectF track(
		left,
		trackTop,
		qMax<qreal>(0.0, right - left),
		trackBottom - trackTop);

	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient trackGradient(track.topLeft(), track.bottomLeft());
	trackGradient.setColorAt(0.0, enabledInk(this, shadow.darker(145), 220));
	trackGradient.setColorAt(1.0, enabledInk(this, shadow, 190));
	painter.setPen(QPen(enabledInk(this, shadow, 220), physicalPixel(this)));
	painter.setBrush(trackGradient);
	painter.drawRoundedRect(track, 1.5, 1.5);

	drawCrispHorizontalLine(
		painter,
		this,
		left + 1.0,
		right - 1.0,
		trackBottom,
		enabledInk(this, highlight, 100));

	static const int tickValues[] = { -24, -18, -12, -6, 0 };
	const QString tickTexts[] = {
		tr("-24"),
		tr("-18"),
		tr("-12"),
		tr("-6"),
		tr("0")
	};

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
			trackBottom + 2.0,
			trackBottom + (stop ? 8.0 : 6.0),
			enabledInk(this, stop ? textInk : mutedInk, 190));
	}

	if (trimAvailable)
	{
		const double boundedTrim = std::clamp(trimDb, -24.0, 0.0);
		const qreal fraction = qreal((boundedTrim + 24.0) / 24.0);
		const qreal markerX = left + fraction * (right - left);

		painter.setPen(QPen(
			enabledInk(this, accentInk, 220),
			3.0 * physicalPixel(this)));
		painter.drawLine(
			QPointF(crispCoordinate(this, markerX),
				crispCoordinate(this, trackTop + 1.0)),
			QPointF(crispCoordinate(this, right - 1.0),
				crispCoordinate(this, trackTop + 1.0)));

		drawCrispVerticalLine(
			painter,
			this,
			markerX,
			trackTop - 4.0,
			trackBottom + 2.0,
			enabledInk(this, accentInk),
			2.0);
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setFont(scaleFont());
	painter.setPen(enabledInk(this, mutedInk, 210));
	const QFontMetrics metrics(scaleFont());
	const qreal labelTop = trackBottom + 8.0;

	for (int index = 0; index < 5; ++index)
	{
		const qreal fraction = qreal(tickValues[index] + 24) / 24.0;
		const qreal x = left + fraction * (right - left);
		const int labelWidth = metrics.horizontalAdvance(tickTexts[index]) + 4;
		const QRectF labelRect(
			x - labelWidth / 2.0,
			labelTop,
			labelWidth,
			height() - labelTop);
		painter.drawText(
			labelRect,
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
			tickTexts[index]);
	}
}

// ---- RackBassManagementCardView ------------------------------------------

RackBassManagementCardView::RackBassManagementCardView(QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(QStringLiteral("RackBassManagementCardView"));
	setAutoFillBackground(false);

	const int sideMargin = GUIHelper::scale(24.0);
	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(sideMargin, 7, sideMargin, 7);
	root->setSpacing(4);

	headerWidget = new QWidget(this);
	headerWidget->setObjectName(QStringLiteral("RackBassHeader"));
	QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(8);

	validityLabel = new QLabel(headerWidget);
	validityLabel->setObjectName(QStringLiteral("RackBassValidity"));
	validityLabel->setFont(rackFont(9, true, 1.0));
	validityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	validityLabel->setAccessibleName(tr("Bass-management validity"));
	headerLayout->addWidget(validityLabel, 0, Qt::AlignVCenter);

	layoutLabel = new QLabel(headerWidget);
	layoutLabel->setObjectName(QStringLiteral("RackBassLayout"));
	layoutLabel->setFont(rackFont(12, true, 0.4));
	layoutLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	headerLayout->addWidget(layoutLabel, 1, Qt::AlignVCenter);

	profileLabel = new QLabel(headerWidget);
	profileLabel->setObjectName(QStringLiteral("RackBassProfile"));
	profileLabel->setFont(rackFont(9, false, 0.2));
	profileLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	profileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	profileLabel->setAccessibleName(tr("Bass-management profile"));
	headerLayout->addWidget(profileLabel, 0, Qt::AlignVCenter);

	actionHost = new QWidget(headerWidget);
	actionHost->setObjectName(QStringLiteral("RackBassActionHost"));
	actionHost->setAccessibleName(tr("Bass-management actions"));
	actionLayout = new QHBoxLayout(actionHost);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	headerLayout->addWidget(actionHost, 0, Qt::AlignVCenter);

	root->addWidget(headerWidget);

	instrumentWidget = new QWidget(this);
	instrumentWidget->setObjectName(QStringLiteral("RackBassInstrumentRow"));
	QHBoxLayout* instrumentLayout = new QHBoxLayout(instrumentWidget);
	instrumentLayout->setContentsMargins(0, 0, 0, 0);
	instrumentLayout->setSpacing(8);

	highPassReadout = new RackCrossoverReadout(instrumentWidget);
	instrumentLayout->addWidget(highPassReadout, 1, Qt::AlignVCenter);

	lowPassReadout = new RackCrossoverReadout(instrumentWidget);
	instrumentLayout->addWidget(lowPassReadout, 1, Qt::AlignVCenter);

	lfeLamp = new RackLfeLamp(instrumentWidget);
	instrumentLayout->addWidget(lfeLamp, 0, Qt::AlignVCenter);

	headroomMeter = new RackHeadroomMeter(instrumentWidget);
	instrumentLayout->addWidget(headroomMeter, 2, Qt::AlignVCenter);

	countsLabel = new QLabel(instrumentWidget);
	countsLabel->setObjectName(QStringLiteral("RackBassCounts"));
	countsLabel->setFont(rackMonoFont(8, true, 0.4));
	countsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	countsLabel->setWordWrap(true);
	countsLabel->setMinimumWidth(GUIHelper::scale(142.0));
	countsLabel->setAccessibleName(tr("Bass path counts"));
	instrumentLayout->addWidget(countsLabel, 0, Qt::AlignVCenter);

	root->addWidget(instrumentWidget);

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(QStringLiteral("RackBassStatus"));
	statusLabel->setFont(rackFont(9, true, 0.1));
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
			update();
			highPassReadout->update();
			lowPassReadout->update();
			lfeLamp->update();
			headroomMeter->update();
			headerWidget->update();
			instrumentWidget->update();
			statusLabel->update();
		});

	updateResponsiveLayout();
}

void RackBassManagementCardView::addActionButton(QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(actionHost);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setProperty("rackBassAction", true);

	const int targetSize = GUIHelper::scale(40.0);
	button->setMinimumSize(
		qMax(button->minimumWidth(), targetSize),
		qMax(button->minimumHeight(), targetSize));

	if (button->accessibleName().trimmed().isEmpty())
	{
		QString actionName = button->text();
		actionName.remove(QLatin1Char('&'));

		if (actionName.trimmed().isEmpty())
			actionName = button->toolTip();

		if (actionName.trimmed().isEmpty())
			actionName = tr("Bass-management action");

		button->setAccessibleName(actionName);
	}

	if (button->toolTip().trimmed().isEmpty())
		button->setToolTip(button->accessibleName());

	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
	updateResponsiveLayout();
}

void RackBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool invalid = !state.valid || !state.errorText.isEmpty();
	const bool hasWarning = !state.warningText.isEmpty() || state.profileMissing;

	validityLabel->setText(invalid
		? tr("! INVALID")
		: tr("OK - READY"));
	validityLabel->setToolTip(invalid
		? tr("Bass-management state is invalid")
		: tr("Bass-management state is valid"));

	const QString layoutText = state.layoutLabel.trimmed().isEmpty()
		? tr("Unknown layout")
		: state.layoutLabel;
	layoutLabel->setText(tr("LAYOUT  %1").arg(layoutText));
	layoutLabel->setToolTip(
		tr("Physical speaker layout: %1").arg(layoutText));

	highPassReadout->setReadout(
		tr("HP"),
		state.representativeHighPass.trimmed().isEmpty()
			? tr("None")
			: state.representativeHighPass);
	lowPassReadout->setReadout(
		tr("LP"),
		state.representativeLowPass.trimmed().isEmpty()
			? tr("None")
			: state.representativeLowPass);

	lfeLamp->setLfeState(
		state.sourceLfePreserved,
		state.sourceLfeGainDb);
	headroomMeter->setHeadroom(
		state.headroomAuto,
		state.headroomTrimDb);

	countsLabel->setText(
		tr("GROUPS %1\nBASS PATHS %2\nROUTES %3")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount)
			.arg(state.activeMatrixEdges));
	countsLabel->setToolTip(
		tr("%1 speaker groups, %2 bass paths and %3 active matrix routes")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount)
			.arg(state.activeMatrixEdges));

	QString profileText;
	QString profileDescription;
	if (state.linkedProfile)
	{
		const QString name = state.profileName.trimmed().isEmpty()
			? tr("Unnamed profile")
			: state.profileName;
		if (state.profileMissing)
		{
			profileText = tr("! MISSING PROFILE  %1").arg(name);
			profileDescription = tr("Linked profile is missing: %1").arg(name);
		}
		else
		{
			profileText = tr("LINKED  %1").arg(name);
			profileDescription = tr("Linked bass-management profile: %1").arg(name);
		}
	}
	else
	{
		const QString name = state.profileName.trimmed().isEmpty()
			? tr("Embedded state")
			: state.profileName;
		profileText = tr("LOCAL  %1").arg(name);
		profileDescription = tr("Embedded bass-management state: %1").arg(name);
	}
	profileLabel->setText(profileText);
	profileLabel->setToolTip(profileDescription);

	QStringList statusLines;
	if (!state.errorText.isEmpty())
	{
		statusLines.append(tr("ERROR: %1").arg(state.errorText));
	}
	else if (!state.valid)
	{
		statusLines.append(tr("ERROR: Invalid bass-management state"));
	}

	if (!state.warningText.isEmpty())
		statusLines.append(tr("WARNING: %1").arg(state.warningText));

	if (state.profileMissing)
	{
		const QString name = state.profileName.trimmed().isEmpty()
			? tr("Unnamed profile")
			: state.profileName;
		statusLines.append(
			tr("WARNING: Linked profile is missing: %1").arg(name));
	}

	statusLabel->setText(statusLines.join(QLatin1Char('\n')));
	statusLabel->setToolTip(statusLines.join(QLatin1Char('\n')));
	statusLabel->setVisible(!statusLines.isEmpty());

	updateLabelPalettes(invalid, hasWarning);
	updateResponsiveLayout();
	update();
}

void RackBassManagementCardView::paintEvent(QPaintEvent* event)
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

	painter.setPen(QPen(enabledInk(this, border, 145), physicalPixel(this)));
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
	const qreal slotAngles[] = { -0.25, 0.42, 0.18, -0.48 };

	for (int index = 0; index < 4; ++index)
	{
		const QPointF center = screwCenters[index];
		QRadialGradient screwGradient(
			center - QPointF(screwRadius * 0.35, screwRadius * 0.35),
			screwRadius * 1.7);
		screwGradient.setColorAt(0.0, highlight);
		screwGradient.setColorAt(0.55, panel.lighter(dark ? 125 : 108));
		screwGradient.setColorAt(1.0, shadow);

		painter.setPen(QPen(
			enabledInk(this, shadow, 185),
			physicalPixel(this)));
		painter.setBrush(screwGradient);
		painter.drawEllipse(center, screwRadius, screwRadius);

		const qreal dx = std::cos(slotAngles[index]) * screwRadius * 0.62;
		const qreal dy = std::sin(slotAngles[index]) * screwRadius * 0.62;
		painter.setPen(QPen(
			enabledInk(this, shadow, 215),
			physicalPixel(this)));
		painter.drawLine(
			center - QPointF(dx, dy),
			center + QPointF(dx, dy));
	}
}

void RackBassManagementCardView::resizeEvent(QResizeEvent* event)
{
	BassManagementCardView::resizeEvent(event);
	updateResponsiveLayout();
}

void RackBassManagementCardView::updateResponsiveLayout()
{
	const int availableWidth = width();
	const bool showProfile =
		availableWidth >= GUIHelper::scale(690.0);
	const bool showCounts =
		availableWidth >= GUIHelper::scale(720.0);
	const bool showMeter =
		availableWidth >= GUIHelper::scale(570.0);
	const bool showLfe =
		availableWidth >= GUIHelper::scale(450.0);

	profileLabel->setVisible(showProfile);
	countsLabel->setVisible(showCounts);
	headroomMeter->setVisible(showMeter);
	lfeLamp->setVisible(showLfe);

	highPassReadout->setVisible(true);
	lowPassReadout->setVisible(true);
	actionHost->setVisible(true);
	validityLabel->setVisible(true);
	layoutLabel->setVisible(true);
}

void RackBassManagementCardView::updateLabelPalettes(
	bool invalid,
	bool warning)
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();

	QPalette validityPalette = validityLabel->palette();
	validityPalette.setColor(
		QPalette::WindowText,
		QColor(invalid ? tokens.danger : tokens.accent2));
	validityLabel->setPalette(validityPalette);

	QPalette layoutPalette = layoutLabel->palette();
	layoutPalette.setColor(QPalette::WindowText, QColor(tokens.text));
	layoutLabel->setPalette(layoutPalette);

	QPalette profilePalette = profileLabel->palette();
	profilePalette.setColor(
		QPalette::WindowText,
		QColor(warning ? tokens.warning : tokens.mutedText));
	profileLabel->setPalette(profilePalette);

	QPalette countsPalette = countsLabel->palette();
	countsPalette.setColor(QPalette::WindowText, QColor(tokens.mutedText));
	countsLabel->setPalette(countsPalette);

	QPalette statusPalette = statusLabel->palette();
	statusPalette.setColor(
		QPalette::WindowText,
		QColor(invalid ? tokens.danger : tokens.warning));
	statusLabel->setPalette(statusPalette);
}
