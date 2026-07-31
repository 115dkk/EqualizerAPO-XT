/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#include "StudioBassManagementCardView.h"

#include <algorithm>
#include <cmath>

#include <QAbstractButton>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"

namespace
{
QColor withOpacity(const QColor& source, qreal opacity)
{
	QColor color = source;
	color.setAlpha(qBound(
		0,
		qRound(source.alpha()
			* std::clamp(opacity, 0.0, 1.0)),
		255));
	return color;
}

qreal devicePixelCenter(qreal coordinate, qreal devicePixelRatio)
{
	return (std::floor(coordinate * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
}

QFont studioMonoFont(
	const QWidget* widget,
	QFont::Weight weight = QFont::Normal)
{
	QFont font = widget != nullptr
		? widget->font()
		: QFont();

	const QString family =
		SkinManager::instance()->tokens().monoFontFamily;
	if (!family.isEmpty())
		font.setFamily(family);
	else
		font.setStyleHint(QFont::Monospace);

	font.setFixedPitch(true);
	font.setWeight(weight);
	return font;
}

void paintGlassSurface(
	QPainter& painter,
	const QWidget* widget,
	const QRectF& bounds)
{
	const qreal dpr = widget->devicePixelRatioF();
	const qreal pixel = 1.0 / dpr;
	const qreal enabledFactor =
		widget->isEnabled() ? 1.0 : 0.45;
	const QPalette palette = widget->palette();

	const QRectF frame = bounds.adjusted(
		pixel / 2.0,
		pixel / 2.0,
		-pixel / 2.0,
		-pixel / 2.0);

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withOpacity(
		palette.color(QPalette::Base),
		0.58 * enabledFactor));
	painter.drawRoundedRect(frame, 8.0, 8.0);

	QPen borderPen(
		withOpacity(
			palette.color(QPalette::Mid),
			0.72 * enabledFactor),
		pixel);
	borderPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(borderPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame, 8.0, 8.0);

	const qreal reflectionY =
		devicePixelCenter(frame.top() + pixel, dpr);
	QPen reflectionPen(
		withOpacity(
			palette.color(QPalette::Light),
			0.30 * enabledFactor),
		pixel);
	reflectionPen.setCapStyle(Qt::FlatCap);
	painter.setPen(reflectionPen);
	painter.drawLine(
		QPointF(frame.left() + 8.0, reflectionY),
		QPointF(frame.right() - 8.0, reflectionY));

	const qreal shadeY =
		devicePixelCenter(frame.bottom() - pixel, dpr);
	QPen shadePen(
		withOpacity(
			palette.color(QPalette::Dark),
			0.24 * enabledFactor),
		pixel);
	shadePen.setCapStyle(Qt::FlatCap);
	painter.setPen(shadePen);
	painter.drawLine(
		QPointF(frame.left() + 8.0, shadeY),
		QPointF(frame.right() - 8.0, shadeY));
}

void drawGlowPath(
	QPainter& painter,
	const QPainterPath& path,
	const QColor& color,
	bool luminous,
	qreal intensity = 1.0)
{
	intensity = std::clamp(intensity, 0.0, 1.0);

	if (luminous)
	{
		QPen outerPen(
			withOpacity(color, 0.12 * intensity),
			8.0);
		outerPen.setCapStyle(Qt::RoundCap);
		outerPen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(outerPen);
		painter.drawPath(path);

		QPen middlePen(
			withOpacity(color, 0.30 * intensity),
			4.0);
		middlePen.setCapStyle(Qt::RoundCap);
		middlePen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(middlePen);
		painter.drawPath(path);
	}

	QPen corePen(
		withOpacity(
			color,
			(luminous ? 0.98 : 0.74) * intensity),
		luminous ? 1.6 : 1.35);
	corePen.setCapStyle(Qt::RoundCap);
	corePen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(corePen);
	painter.drawPath(path);
}

double representativeCornerFrequency(const QString& text)
{
	if (text.trimmed().isEmpty())
		return 0.0;

	static const QRegularExpression frequencyExpression(
		QStringLiteral(
			R"((\d+(?:[.,]\d+)?)\s*(k)?\s*hz\b)"),
		QRegularExpression::CaseInsensitiveOption);

	const QRegularExpressionMatch frequencyMatch =
		frequencyExpression.match(text);
	if (frequencyMatch.hasMatch())
	{
		QString numberText = frequencyMatch.captured(1);
		numberText.replace(
			QLatin1Char(','),
			QLatin1Char('.'));

		bool ok = false;
		double frequency = numberText.toDouble(&ok);
		if (!ok)
			return 0.0;

		if (!frequencyMatch.captured(2).isEmpty())
			frequency *= 1000.0;

		return std::clamp(
			frequency,
			10.0,
			40000.0);
	}

	static const QRegularExpression numberExpression(
		QStringLiteral(R"((\d+(?:[.,]\d+)?))"));

	double largestNumber = 0.0;
	QRegularExpressionMatchIterator matches =
		numberExpression.globalMatch(text);
	while (matches.hasNext())
	{
		QString numberText = matches.next().captured(1);
		numberText.replace(
			QLatin1Char(','),
			QLatin1Char('.'));

		bool ok = false;
		const double number = numberText.toDouble(&ok);
		if (ok)
			largestNumber =
				std::max(largestNumber, number);
	}

	if (largestNumber <= 0.0)
		return 0.0;

	return std::clamp(
		largestNumber,
		10.0,
		40000.0);
}

void setStyledProperty(
	QWidget* widget,
	const char* name,
	const QString& value)
{
	if (widget == nullptr
		|| widget->property(name).toString() == value)
	{
		return;
	}

	widget->setProperty(name, value);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->updateGeometry();
	widget->update();
}

QString countText(
	int count,
	const QString& singular,
	const QString& plural)
{
	return count == 1
		? singular
		: plural.arg(count);
}
}

class RightElidedLabel : public QLabel
{
public:
	explicit RightElidedLabel(QWidget* parent = nullptr)
		: QLabel(parent)
	{
		setSizePolicy(
			QSizePolicy::Expanding,
			QSizePolicy::Preferred);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setFullText(const QString& text)
	{
		if (fullText == text)
			return;

		fullText = text;
		updateDisplayedText();
		updateGeometry();
	}

	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		const QMargins margins = contentsMargins();
		return QSize(
			metrics.horizontalAdvance(fullText)
				+ margins.left()
				+ margins.right(),
			std::max(
				QLabel::sizeHint().height(),
				metrics.height()
					+ margins.top()
					+ margins.bottom()));
	}

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		updateDisplayedText();
	}

	bool event(QEvent* event) override
	{
		const bool handled = QLabel::event(event);

		switch (event->type())
		{
		case QEvent::FontChange:
		case QEvent::StyleChange:
		case QEvent::Polish:
		case QEvent::Show:
			updateDisplayedText();
			break;

		default:
			break;
		}

		return handled;
	}

private:
	void updateDisplayedText()
	{
		const int availableWidth =
			std::max(0, contentsRect().width());
		const QFontMetrics metrics(font());

		QLabel::setText(metrics.elidedText(
			fullText,
			Qt::ElideRight,
			availableWidth));
	}

	QString fullText;
};

class ProfileSummaryWidget : public QWidget
{
public:
	explicit ProfileSummaryWidget(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(
			QStringLiteral("StudioBassProfileSummary"));
		setMinimumWidth(140);
		setMinimumHeight(26);
		setSizePolicy(
			QSizePolicy::Expanding,
			QSizePolicy::Preferred);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setParts(
		const QString& prefixText,
		const QString& nameText,
		const QString& suffixText,
		const QString& completeText)
	{
		prefix = prefixText;
		name = nameText;
		suffix = suffixText;
		fullText = completeText;

		setToolTip(fullText);
		setAccessibleDescription(fullText);
		updateGeometry();
		update();
	}

	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		const int gap =
			metrics.horizontalAdvance(QLatin1Char(' '));

		int preferredWidth =
			metrics.horizontalAdvance(prefix)
			+ metrics.horizontalAdvance(name);
		if (!suffix.isEmpty())
		{
			preferredWidth += gap
				+ metrics.horizontalAdvance(suffix);
		}

		return QSize(
			std::max(180, preferredWidth),
			std::max(26, metrics.height() + 8));
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		const QRect content =
			rect().adjusted(0, 2, 0, -2);
		const int availableWidth = content.width();
		if (availableWidth <= 0 || name.isEmpty())
			return;

		const QFontMetrics metrics(font());
		const int gap =
			metrics.horizontalAdvance(QLatin1Char(' '));
		const int prefixWidth =
			metrics.horizontalAdvance(prefix);
		const int suffixWidth =
			metrics.horizontalAdvance(suffix);
		const int minimumNameWidth =
			std::min(
				metrics.horizontalAdvance(name),
				metrics.horizontalAdvance(
					QStringLiteral("M...")));

		bool showSuffix =
			!suffix.isEmpty()
			&& availableWidth
				>= suffixWidth
					+ gap
					+ minimumNameWidth;

		const int suffixReserve = showSuffix
			? suffixWidth + gap
			: 0;

		const bool showPrefix =
			!prefix.isEmpty()
			&& availableWidth - suffixReserve
				>= prefixWidth
					+ minimumNameWidth;

		const int prefixReserve = showPrefix
			? prefixWidth
			: 0;
		const int nameWidth = std::max(
			0,
			availableWidth
				- prefixReserve
				- suffixReserve);

		const QString displayedName =
			metrics.elidedText(
				name,
				Qt::ElideRight,
				nameWidth);
		if (displayedName.isEmpty())
			return;

		QPainter painter(this);
		painter.setRenderHint(
			QPainter::TextAntialiasing,
			true);
		painter.setFont(font());

		QColor ink =
			palette().color(QPalette::Text);
		if (!isEnabled())
			ink = withOpacity(ink, 0.45);
		painter.setPen(ink);

		int x = content.left();

		if (showPrefix)
		{
			painter.drawText(
				QRect(
					x,
					content.top(),
					prefixWidth,
					content.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				prefix);
			x += prefixWidth;
		}

		const int displayedNameWidth =
			metrics.horizontalAdvance(displayedName);
		painter.drawText(
			QRect(
				x,
				content.top(),
				std::min(nameWidth, displayedNameWidth),
				content.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			displayedName);
		x += displayedNameWidth;

		if (showSuffix)
		{
			x += gap;
			painter.drawText(
				QRect(
					x,
					content.top(),
					suffixWidth,
					content.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				suffix);
		}
	}

private:
	QString prefix;
	QString name;
	QString suffix;
	QString fullText;
};

class GlowReadoutWidget : public QWidget
{
public:
	explicit GlowReadoutWidget(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(
			QStringLiteral("StudioBassHeadroomReadout"));
		setMinimumSize(152, 42);
		setSizePolicy(
			QSizePolicy::Fixed,
			QSizePolicy::Fixed);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setText(const QString& value)
	{
		if (readoutText == value)
			return;

		readoutText = value;
		updateGeometry();
		update();
	}

	void setAutomatic(bool automatic)
	{
		if (autoMode == automatic)
			return;

		autoMode = automatic;
		update();
	}

	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		return QSize(
			std::max(
				152,
				metrics.horizontalAdvance(readoutText)
					+ 24),
			std::max(42, metrics.height() + 18));
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		paintGlassSurface(
			painter,
			this,
			QRectF(rect()));
		painter.setRenderHint(
			QPainter::Antialiasing,
			true);

		const QRect textRect =
			rect().adjusted(12, 7, -12, -7);
		const QFontMetrics metrics(font());
		const QString displayedText =
			metrics.elidedText(
				readoutText,
				Qt::ElideRight,
				std::max(0, textRect.width()));

		if (displayedText.isEmpty())
			return;

		const QFontMetricsF floatingMetrics(font());
		const qreal textWidth =
			floatingMetrics.horizontalAdvance(
				displayedText);
		const qreal x =
			textRect.left()
			+ (textRect.width() - textWidth) / 2.0;
		const qreal y =
			textRect.top()
			+ (textRect.height()
				- floatingMetrics.height()) / 2.0
			+ floatingMetrics.ascent();

		QPainterPath glyphs;
		glyphs.addText(
			QPointF(x, y),
			font(),
			displayedText);

		if (autoMode && isEnabled())
		{
			const QColor accent =
				palette().color(QPalette::Highlight);

			painter.setBrush(Qt::NoBrush);

			QPen outerGlow(
				withOpacity(accent, 0.15),
				6.0);
			outerGlow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(outerGlow);
			painter.drawPath(glyphs);

			QPen innerGlow(
				withOpacity(accent, 0.36),
				2.5);
			innerGlow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(innerGlow);
			painter.drawPath(glyphs);

			painter.fillPath(
				glyphs,
				withOpacity(accent, 0.98));
		}
		else
		{
			QColor ink =
				palette().color(QPalette::Text);
			if (!isEnabled())
				ink = withOpacity(ink, 0.45);
			painter.fillPath(glyphs, ink);
		}
	}

private:
	QString readoutText;
	bool autoMode = false;
};

class BassInstrumentWidget : public QWidget
{
public:
	explicit BassInstrumentWidget(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(
			QStringLiteral("StudioBassInstrument"));
		setMinimumHeight(102);
		setSizePolicy(
			QSizePolicy::Expanding,
			QSizePolicy::Fixed);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setPresentation(
		bool stateValid,
		bool sourceLfeIsPreserved,
		const QString& highPass,
		const QString& lowPass,
		const QString& mainCaptionText,
		const QString& bassCaptionText,
		const QString& lfeCaptionText,
		const QString& highPassCaptionText,
		const QString& lowPassCaptionText,
		const QString& description)
	{
		valid = stateValid;
		sourceLfePreserved = sourceLfeIsPreserved;
		highPassFrequency =
			representativeCornerFrequency(highPass);
		lowPassFrequency =
			representativeCornerFrequency(lowPass);
		mainCaption = mainCaptionText;
		bassCaption = bassCaptionText;
		lfeCaption = lfeCaptionText;
		highPassCaption = highPassCaptionText;
		lowPassCaption = lowPassCaptionText;

		setAccessibleDescription(description);
		update();
	}

	QSize sizeHint() const override
	{
		return QSize(440, 104);
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		paintGlassSurface(
			painter,
			this,
			QRectF(rect()));
		painter.setRenderHint(
			QPainter::Antialiasing,
			true);

		const qreal dpr = devicePixelRatioF();
		const qreal pixel = 1.0 / dpr;
		const QRectF bounds =
			QRectF(rect()).adjusted(
				10.0,
				10.0,
				-10.0,
				-10.0);

		if (bounds.width() <= 0.0
			|| bounds.height() <= 0.0)
		{
			return;
		}

		QFont captionFont =
			studioMonoFont(this, QFont::Medium);
		if (captionFont.pointSizeF() > 0.0)
		{
			captionFont.setPointSizeF(
				std::max(
					7.0,
					captionFont.pointSizeF() - 1.0));
		}

		const QFontMetricsF metrics(captionFont);
		const qreal rowHeight =
			std::ceil(metrics.height()) + 2.0;
		const qreal labelWidth = std::ceil(
			std::max({
				metrics.horizontalAdvance(mainCaption),
				metrics.horizontalAdvance(bassCaption),
				metrics.horizontalAdvance(lfeCaption)
			})) + 3.0;

		const qreal highPassWidth =
			std::ceil(
				metrics.horizontalAdvance(
					highPassCaption))
			+ 2.0;
		const qreal lowPassWidth =
			std::ceil(
				metrics.horizontalAdvance(
					lowPassCaption))
			+ 2.0;

		const qreal minimumResponseWidth =
			std::max(
				92.0,
				highPassWidth
					+ lowPassWidth
					+ 10.0);
		const qreal minimumTraceWidth = 34.0;
		const qreal desiredSplit =
			bounds.left()
			+ std::max(
				labelWidth
					+ 10.0
					+ minimumTraceWidth,
				bounds.width() * 0.62);

		const bool responseFits =
			bounds.width()
				>= labelWidth
					+ 10.0
					+ minimumTraceWidth
					+ 10.0
					+ minimumResponseWidth;

		const qreal splitX = responseFits
			? std::clamp(
				desiredSplit,
				bounds.left()
					+ labelWidth
					+ 10.0
					+ minimumTraceWidth,
				bounds.right()
					- minimumResponseWidth
					- 8.0)
			: bounds.right();

		const QColor accent =
			palette().color(QPalette::Highlight);
		const QColor text =
			palette().color(QPalette::Text);
		const QColor quiet =
			palette().color(QPalette::Mid);

		const qreal enabledFactor =
			isEnabled() ? 1.0 : 0.45;
		const bool luminous = valid && isEnabled();

		painter.setFont(captionFont);
		painter.setPen(
			withOpacity(text, 0.86 * enabledFactor));

		const QRectF mainLabelRect(
			bounds.left(),
			bounds.top(),
			labelWidth,
			rowHeight);
		const QRectF bassLabelRect(
			bounds.left(),
			bounds.center().y() - rowHeight / 2.0,
			labelWidth,
			rowHeight);
		const QRectF lfeLabelRect(
			bounds.left(),
			bounds.bottom() - rowHeight,
			labelWidth,
			rowHeight);

		painter.drawText(
			mainLabelRect,
			Qt::AlignLeft | Qt::AlignVCenter,
			mainCaption);
		painter.drawText(
			bassLabelRect,
			Qt::AlignLeft | Qt::AlignVCenter,
			bassCaption);
		painter.drawText(
			lfeLabelRect,
			Qt::AlignLeft | Qt::AlignVCenter,
			lfeCaption);

		const qreal mainY = devicePixelCenter(
			mainLabelRect.center().y(),
			dpr);
		const qreal bassY = devicePixelCenter(
			bassLabelRect.center().y(),
			dpr);
		const qreal lfeY = devicePixelCenter(
			lfeLabelRect.center().y(),
			dpr);

		const qreal traceStart =
			bounds.left() + labelWidth + 9.0;
		const qreal traceEnd = responseFits
			? splitX - 11.0
			: bounds.right() - 4.0;

		if (traceEnd > traceStart)
		{
			QPainterPath mainTrace;
			mainTrace.moveTo(traceStart, mainY);
			mainTrace.lineTo(traceEnd, mainY);
			drawGlowPath(
				painter,
				mainTrace,
				accent,
				luminous,
				0.96 * enabledFactor);

			QPainterPath bassTrace;
			bassTrace.moveTo(traceStart, bassY);
			bassTrace.lineTo(traceEnd, bassY);
			drawGlowPath(
				painter,
				bassTrace,
				accent,
				luminous,
				0.86 * enabledFactor);

			const qreal lfeStart =
				bounds.left() + labelWidth + 2.0;
			QPainterPath lfeTrace;
			lfeTrace.moveTo(lfeStart, lfeY);
			lfeTrace.cubicTo(
				lfeStart + 12.0,
				lfeY,
				traceStart - 12.0,
				bassY,
				traceStart,
				bassY);

			if (sourceLfePreserved)
			{
				drawGlowPath(
					painter,
					lfeTrace,
					accent,
					luminous,
					0.78 * enabledFactor);
			}
			else
			{
				QPen offPen(
					withOpacity(
						accent,
						0.58 * enabledFactor),
					pixel);
				offPen.setDashPattern({ 3.0, 3.0 });
				offPen.setCapStyle(Qt::FlatCap);
				painter.setPen(offPen);
				painter.drawPath(lfeTrace);
			}
		}

		if (!responseFits)
			return;

		const qreal dividerX =
			devicePixelCenter(splitX, dpr);
		QPen dividerPen(
			withOpacity(
				quiet,
				0.62 * enabledFactor),
			pixel);
		dividerPen.setCapStyle(Qt::FlatCap);
		painter.setPen(dividerPen);
		painter.drawLine(
			QPointF(
				dividerX,
				bounds.top() + 3.0),
			QPointF(
				dividerX,
				bounds.bottom() - 3.0));

		const QRectF responsePanel(
			splitX + 10.0,
			bounds.top(),
			bounds.right() - splitX - 10.0,
			bounds.height());

		const bool drawBothCaptions =
			highPassWidth
				+ lowPassWidth
				+ 8.0
				<= responsePanel.width();

		painter.setPen(
			withOpacity(text, 0.82 * enabledFactor));

		if (highPassWidth <= responsePanel.width())
		{
			painter.drawText(
				QRectF(
					responsePanel.left(),
					responsePanel.top(),
					highPassWidth,
					rowHeight),
				Qt::AlignLeft | Qt::AlignVCenter,
				highPassCaption);
		}

		if (drawBothCaptions)
		{
			painter.drawText(
				QRectF(
					responsePanel.right()
						- lowPassWidth,
					responsePanel.top(),
					lowPassWidth,
					rowHeight),
				Qt::AlignRight | Qt::AlignVCenter,
				lowPassCaption);
		}

		const QRectF responseBounds(
			responsePanel.left() + 2.0,
			responsePanel.top() + rowHeight + 5.0,
			std::max(
				1.0,
				responsePanel.width() - 4.0),
			std::max(
				1.0,
				responsePanel.height()
					- rowHeight
					- 9.0));

		double minimumCorner = 0.0;
		double maximumCorner = 0.0;

		if (highPassFrequency > 0.0)
		{
			minimumCorner = highPassFrequency;
			maximumCorner = highPassFrequency;
		}

		if (lowPassFrequency > 0.0)
		{
			minimumCorner = minimumCorner > 0.0
				? std::min(
					minimumCorner,
					lowPassFrequency)
				: lowPassFrequency;
			maximumCorner =
				std::max(
					maximumCorner,
					lowPassFrequency);
		}

		if (maximumCorner <= 0.0)
			return;

		const double minimumFrequency =
			std::max(
				10.0,
				minimumCorner / 6.0);
		const double maximumFrequency =
			std::min(
				40000.0,
				std::max(
					500.0,
					maximumCorner * 6.0));
		const double logarithmicMinimum =
			std::log(minimumFrequency);
		const double logarithmicRange =
			std::log(maximumFrequency)
			- logarithmicMinimum;

		auto buildResponsePath =
			[&responseBounds,
				logarithmicMinimum,
				logarithmicRange](
				double corner,
				bool highPass)
			{
				QPainterPath responsePath;
				constexpr int sampleCount = 64;

				for (int sample = 0;
					sample < sampleCount;
					++sample)
				{
					const double position =
						static_cast<double>(sample)
						/ (sampleCount - 1);
					const double frequency =
						std::exp(
							logarithmicMinimum
							+ position
								* logarithmicRange);
					const double ratio =
						frequency / corner;
					const double fourthPower =
						std::pow(ratio, 4.0);
					const double magnitude =
						highPass
							? std::sqrt(
								fourthPower
								/ (1.0
									+ fourthPower))
							: 1.0
								/ std::sqrt(
									1.0
									+ fourthPower);
					const double decibels =
						20.0
						* std::log10(
							std::max(
								0.0316227766,
								magnitude));
					const double normalized =
						std::clamp(
							(decibels + 30.0)
								/ 30.0,
							0.0,
							1.0);

					const QPointF point(
						responseBounds.left()
							+ position
								* responseBounds.width(),
						responseBounds.bottom()
							- normalized
								* responseBounds.height());

					if (sample == 0)
						responsePath.moveTo(point);
					else
						responsePath.lineTo(point);
				}

				return responsePath;
			};

		painter.save();
		painter.setClipRect(
			responseBounds.adjusted(
				-8.0,
				-8.0,
				8.0,
				8.0));

		if (highPassFrequency > 0.0)
		{
			drawGlowPath(
				painter,
				buildResponsePath(
					highPassFrequency,
					true),
				accent,
				luminous,
				0.96 * enabledFactor);
		}

		if (lowPassFrequency > 0.0)
		{
			drawGlowPath(
				painter,
				buildResponsePath(
					lowPassFrequency,
					false),
				accent,
				luminous,
				0.76 * enabledFactor);
		}

		painter.restore();
	}

private:
	bool valid = false;
	bool sourceLfePreserved = false;
	double highPassFrequency = 0.0;
	double lowPassFrequency = 0.0;
	QString mainCaption;
	QString bassCaption;
	QString lfeCaption;
	QString highPassCaption;
	QString lowPassCaption;
};

StudioBassManagementCardView::StudioBassManagementCardView(
	QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(
		QStringLiteral("StudioBassManagementCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(7);

	QHBoxLayout* captionLayout = new QHBoxLayout();
	captionLayout->setContentsMargins(0, 0, 0, 0);
	captionLayout->setSpacing(8);

	validityChip = new QLabel(this);
	validityChip->setObjectName(
		QStringLiteral("StudioBassValidity"));
	validityChip->setAlignment(Qt::AlignCenter);
	validityChip->setMinimumHeight(28);
	validityChip->setSizePolicy(
		QSizePolicy::Fixed,
		QSizePolicy::Fixed);
	validityChip->setAccessibleName(
		tr("Bass-management validity"));
	validityChip->setToolTip(
		tr("Validation status of the bass-management state"));
	validityChip->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	captionLayout->addWidget(
		validityChip,
		0,
		Qt::AlignVCenter);

	layoutLabel = new RightElidedLabel(this);
	layoutLabel->setObjectName(
		QStringLiteral("StudioBassLayout"));
	layoutLabel->setAccessibleName(
		tr("Speaker layout"));
	layoutLabel->setMinimumWidth(110);
	captionLayout->addWidget(
		layoutLabel,
		1,
		Qt::AlignVCenter);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	captionLayout->addLayout(actionLayout);

	root->addLayout(captionLayout);

	crossoverLabel = new RightElidedLabel(this);
	crossoverLabel->setObjectName(
		QStringLiteral("StudioBassCrossovers"));
	crossoverLabel->setAccessibleName(
		tr("Representative crossovers"));
	crossoverLabel->setMinimumWidth(100);
	crossoverLabel->setToolTip(
		tr("Representative high-pass and low-pass crossover sections"));
	root->addWidget(crossoverLabel);

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(
		QStringLiteral("StudioBassStatus"));
	statusLabel->setAccessibleName(
		tr("Bass-management status"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	statusLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	root->addWidget(statusLabel);

	factsRow = new QWidget(this);
	factsRow->setObjectName(
		QStringLiteral("StudioBassFactsRow"));

	QHBoxLayout* factsLayout =
		new QHBoxLayout(factsRow);
	factsLayout->setContentsMargins(0, 0, 0, 0);
	factsLayout->setSpacing(6);

	routesChip = new QLabel(factsRow);
	routesChip->setObjectName(
		QStringLiteral("StudioBassFactChip"));
	routesChip->setAlignment(Qt::AlignCenter);
	routesChip->setMinimumHeight(28);
	routesChip->setSizePolicy(
		QSizePolicy::Fixed,
		QSizePolicy::Fixed);
	routesChip->setAccessibleName(
		tr("Active matrix route count"));
	routesChip->setToolTip(
		tr("Number of active matrix routes"));
	routesChip->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	factsLayout->addWidget(
		routesChip,
		0,
		Qt::AlignVCenter);

	lfeChip = new QLabel(factsRow);
	lfeChip->setObjectName(
		QStringLiteral("StudioBassFactChip"));
	lfeChip->setAlignment(Qt::AlignCenter);
	lfeChip->setMinimumHeight(28);
	lfeChip->setSizePolicy(
		QSizePolicy::Fixed,
		QSizePolicy::Fixed);
	lfeChip->setAccessibleName(
		tr("Source LFE routing"));
	lfeChip->setToolTip(
		tr("Whether source LFE is preserved and at what gain"));
	lfeChip->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	factsLayout->addWidget(
		lfeChip,
		0,
		Qt::AlignVCenter);

	profileSummary =
		new ProfileSummaryWidget(factsRow);
	profileSummary->setAccessibleName(
		tr("Bass-management profile"));
	factsLayout->addWidget(
		profileSummary,
		1,
		Qt::AlignVCenter);

	root->addWidget(factsRow);

	QHBoxLayout* instrumentLayout =
		new QHBoxLayout();
	instrumentLayout->setContentsMargins(0, 0, 0, 0);
	instrumentLayout->setSpacing(10);

	instrumentPane = new BassInstrumentWidget(this);
	instrumentPane->setAccessibleName(
		tr("Bass-management signal and response display"));
	instrumentPane->setToolTip(
		tr("Main and bass signal traces with representative crossover responses"));
	instrumentLayout->addWidget(instrumentPane, 1);

	headroomPane = new QWidget(this);
	headroomPane->setObjectName(
		QStringLiteral("StudioBassHeadroomPane"));

	QVBoxLayout* headroomLayout =
		new QVBoxLayout(headroomPane);
	headroomLayout->setContentsMargins(0, 0, 0, 0);
	headroomLayout->setSpacing(4);

	QLabel* headroomCaption = new QLabel(
		tr("HEADROOM"),
		headroomPane);
	headroomCaption->setObjectName(
		QStringLiteral("StudioBassHeadroomCaption"));
	headroomCaption->setAlignment(Qt::AlignCenter);
	headroomCaption->setAccessibleName(
		tr("Headroom"));
	headroomCaption->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	headroomLayout->addWidget(headroomCaption);

	headroomReadout =
		new GlowReadoutWidget(headroomPane);
	headroomReadout->setAccessibleName(
		tr("Headroom trim"));
	headroomReadout->setToolTip(
		tr("Automatic or manual headroom trim"));
	headroomLayout->addWidget(headroomReadout);

	instrumentLayout->addWidget(
		headroomPane,
		0,
		Qt::AlignVCenter);
	root->addLayout(instrumentLayout);

	connect(
		SkinManager::instance(),
		&SkinManager::skinChanged,
		this,
		[this]()
		{
			update();
			layoutLabel->update();
			profileSummary->update();
			crossoverLabel->update();
			instrumentPane->update();
			headroomReadout->update();
			updateResponsiveVisibility(width());
		});

	StudioBassManagementCardView::applyState(state());
	updateResponsiveVisibility(width());
}

void StudioBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(this);
	button->setObjectName(
		QStringLiteral("StudioBassActionButton"));
	button->setMinimumSize(
		std::max(40, button->minimumWidth()),
		std::max(40, button->minimumHeight()));

	if (button->focusPolicy() == Qt::NoFocus)
		button->setFocusPolicy(Qt::StrongFocus);

	QString accessibleName =
		button->accessibleName().trimmed();
	if (accessibleName.isEmpty())
	{
		accessibleName = button->text();
		accessibleName.remove(QLatin1Char('&'));
		accessibleName = accessibleName.trimmed();
	}
	if (accessibleName.isEmpty())
		accessibleName = button->toolTip().trimmed();
	if (accessibleName.isEmpty())
		accessibleName = tr("Bass-management action");

	button->setAccessibleName(accessibleName);

	if (button->toolTip().trimmed().isEmpty())
		button->setToolTip(accessibleName);

	if (button->text().trimmed().isEmpty())
		button->setText(accessibleName);

	actionLayout->addWidget(
		button,
		0,
		Qt::AlignVCenter);
	actionButtons.append(button);

	updateResponsiveVisibility(width());
}

void StudioBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool linkedProfileMissing =
		state.linkedProfile
		&& state.profileMissing;
	const bool effectiveValid =
		state.valid
		&& state.errorText.isEmpty()
		&& !linkedProfileMissing;
	const bool hasWarning =
		!state.warningText.isEmpty();

	if (!effectiveValid)
	{
		validityChip->setText(tr("ERROR"));
		setStyledProperty(
			validityChip,
			"severity",
			QStringLiteral("invalid"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is invalid"));
	}
	else if (hasWarning)
	{
		validityChip->setText(tr("! WARNING"));
		setStyledProperty(
			validityChip,
			"severity",
			QStringLiteral("warning"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is valid with a warning"));
	}
	else
	{
		validityChip->setText(tr("OK VALID"));
		setStyledProperty(
			validityChip,
			"severity",
			QStringLiteral("valid"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is valid"));
	}

	const QString layoutText =
		state.layoutLabel.isEmpty()
			? tr("Unknown")
			: state.layoutLabel;
	const QString layoutSummary =
		tr("Layout: %1").arg(layoutText);
	layoutLabel->setFullText(layoutSummary);
	layoutLabel->setToolTip(
		tr("Speaker layout: %1").arg(layoutText));
	layoutLabel->setAccessibleDescription(layoutSummary);

	QString profileName;
	QString profileSuffix;

	if (state.linkedProfile)
	{
		profileName = state.profileName.isEmpty()
			? tr("Unnamed linked profile")
			: state.profileName;
		profileSuffix = linkedProfileMissing
			? tr("(linked, missing)")
			: tr("(linked)");
	}
	else
	{
		profileName = state.profileName.isEmpty()
			? tr("Embedded state")
			: state.profileName;
	}

	const QString profilePrefix = tr("Profile: ");
	const QString profileText =
		profileSuffix.isEmpty()
			? profilePrefix + profileName
			: profilePrefix
				+ profileName
				+ QLatin1Char(' ')
				+ profileSuffix;

	profileSummary->setParts(
		profilePrefix,
		profileName,
		profileSuffix,
		profileText);

	QStringList crossoverParts;
	if (!state.representativeHighPass.isEmpty())
	{
		crossoverParts.append(
			tr("HP %1").arg(
				state.representativeHighPass));
	}
	if (!state.representativeLowPass.isEmpty())
	{
		crossoverParts.append(
			tr("LP %1").arg(
				state.representativeLowPass));
	}

	const QString crossoverSummary =
		crossoverParts.isEmpty()
			? tr("Crossovers: none")
			: tr("Crossovers: %1").arg(
				crossoverParts.join(
					QStringLiteral(" / ")));
	crossoverLabel->setFullText(crossoverSummary);
	crossoverLabel->setToolTip(crossoverSummary);
	crossoverLabel->setAccessibleDescription(
		crossoverSummary);

	routesChip->setText(countText(
		state.activeMatrixEdges,
		tr("1 route"),
		tr("%1 routes")));
	routesChip->setAccessibleDescription(
		tr("%1 active matrix routes")
			.arg(state.activeMatrixEdges));

	QString lfeText;
	if (state.sourceLfePreserved)
	{
		lfeText = std::isfinite(state.sourceLfeGainDb)
			? tr("LFE %1 dB").arg(
				QString::number(
					state.sourceLfeGainDb,
					'f',
					1))
			: tr("LFE preserved");
	}
	else
	{
		lfeText = tr("LFE not preserved");
	}

	lfeChip->setText(lfeText);
	lfeChip->setAccessibleDescription(lfeText);

	const bool finiteHeadroom =
		std::isfinite(state.headroomTrimDb);
	const QString headroomValue = finiteHeadroom
		? QString::number(
			state.headroomTrimDb,
			'f',
			1)
		: QStringLiteral("--");

	const QString headroomText = state.headroomAuto
		? tr("AUTO %1 dB").arg(headroomValue)
		: tr("MANUAL %1 dB").arg(headroomValue);
	const QString headroomDescription = state.headroomAuto
		? tr("Automatic headroom trim: %1 dB")
			.arg(headroomValue)
		: tr("Manual headroom trim: %1 dB")
			.arg(headroomValue);

	headroomReadout->setText(headroomText);
	headroomReadout->setAccessibleDescription(
		headroomDescription);
	headroomReadout->setAutomatic(state.headroomAuto);

	const QString highPassDescription =
		state.representativeHighPass.isEmpty()
			? tr("not set")
			: state.representativeHighPass;
	const QString lowPassDescription =
		state.representativeLowPass.isEmpty()
			? tr("not set")
			: state.representativeLowPass;
	const QString lfeDescription =
		state.sourceLfePreserved
			? lfeText
			: tr("not preserved");

	const QString instrumentDescription =
		tr("Signal traces. Main high-pass %1. Bass low-pass %2. Source LFE %3.")
			.arg(
				highPassDescription,
				lowPassDescription,
				lfeDescription);

	instrumentPane->setPresentation(
		effectiveValid,
		state.sourceLfePreserved,
		state.representativeHighPass,
		state.representativeLowPass,
		tr("MAIN"),
		tr("BASS"),
		tr("LFE"),
		tr("HP"),
		tr("LP"),
		instrumentDescription);

	QStringList statusLines;
	if (!state.errorText.isEmpty())
	{
		statusLines.append(
			tr("! Error: %1").arg(
				state.errorText));
	}
	else if (!state.valid)
	{
		statusLines.append(
			tr("! Error: The bass-management state is invalid."));
	}

	if (linkedProfileMissing)
	{
		if (state.profileName.isEmpty())
		{
			statusLines.append(
				tr("! Error: The linked profile is missing."));
		}
		else
		{
			statusLines.append(
				tr("! Error: Linked profile %1 is missing.")
					.arg(state.profileName));
		}
	}

	if (hasWarning)
	{
		statusLines.append(
			tr("! Warning: %1").arg(
				state.warningText));
	}

	statusLabel->setText(
		statusLines.join(QLatin1Char('\n')));
	statusLabel->setVisible(!statusLines.isEmpty());
	setStyledProperty(
		statusLabel,
		"severity",
		!effectiveValid
			? QStringLiteral("invalid")
			: hasWarning
				? QStringLiteral("warning")
				: QStringLiteral("none"));

	QStringList accessibleSummary;
	accessibleSummary.append(
		effectiveValid
			? tr("Valid bass-management state")
			: tr("Invalid bass-management state"));
	accessibleSummary.append(
		tr("Layout %1").arg(layoutText));
	accessibleSummary.append(crossoverSummary);
	accessibleSummary.append(
		tr("%1 speaker groups, %2 bass paths, %3 active routes")
			.arg(state.speakerGroupCount)
			.arg(state.bassPathCount)
			.arg(state.activeMatrixEdges));
	accessibleSummary.append(lfeText);
	accessibleSummary.append(headroomDescription);
	accessibleSummary.append(profileText);
	accessibleSummary.append(statusLines);

	setAccessibleDescription(
		accessibleSummary.join(
			QStringLiteral(". ")));

	updateResponsiveVisibility(width());
}

void StudioBassManagementCardView::paintEvent(
	QPaintEvent* event)
{
	QWidget::paintEvent(event);

	if (!hasFocus())
		return;

	QPainter painter(this);
	painter.setRenderHint(
		QPainter::Antialiasing,
		true);

	const qreal dpr = devicePixelRatioF();
	const qreal pixel = 1.0 / dpr;
	const QRectF focusRect =
		QRectF(rect()).adjusted(
			pixel / 2.0,
			pixel / 2.0,
			-pixel / 2.0,
			-pixel / 2.0);
	const QColor focusColor =
		palette().color(QPalette::Highlight);

	QPen glowPen(
		withOpacity(focusColor, 0.22),
		4.0);
	glowPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(glowPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(
		focusRect,
		8.0,
		8.0);

	QPen corePen(
		withOpacity(focusColor, 0.82),
		pixel);
	corePen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(corePen);
	painter.drawRoundedRect(
		focusRect,
		8.0,
		8.0);
}

void StudioBassManagementCardView::resizeEvent(
	QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateResponsiveVisibility(
		event->size().width());
}

void StudioBassManagementCardView::updateResponsiveVisibility(
	int availableWidth)
{
	profileSummary->setVisible(availableWidth >= 600);
	lfeChip->setVisible(availableWidth >= 480);
	instrumentPane->setVisible(availableWidth >= 560);
	headroomPane->setVisible(availableWidth >= 720);

	const bool showActionText =
		availableWidth >= 700;

	for (QAbstractButton* button : actionButtons)
	{
		QToolButton* toolButton =
			qobject_cast<QToolButton*>(button);
		if (toolButton == nullptr)
			continue;

		if (toolButton->icon().isNull())
		{
			toolButton->setToolButtonStyle(
				Qt::ToolButtonTextOnly);
		}
		else
		{
			toolButton->setToolButtonStyle(
				showActionText
					? Qt::ToolButtonTextBesideIcon
					: Qt::ToolButtonIconOnly);
		}
	}
}
