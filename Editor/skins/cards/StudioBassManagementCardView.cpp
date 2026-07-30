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
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
QColor withOpacity(const QColor& source, qreal opacity)
{
	QColor color = source;
	color.setAlpha(qBound(0,
		qRound(source.alpha() * std::clamp(opacity, 0.0, 1.0)),
		255));
	return color;
}

qreal devicePixelCenter(qreal coordinate, qreal devicePixelRatio)
{
	return (std::floor(coordinate * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
}

QFont studioMonoFont(QFont::Weight weight = QFont::Normal)
{
	const QString family =
		SkinManager::instance()->tokens().monoFontFamily;
	QFont font(family);
	if (family.isEmpty())
		font.setStyleHint(QFont::Monospace);
	font.setFixedPitch(true);
	font.setWeight(weight);
	return font;
}

void paintGlassSurface(QPainter& painter, const QWidget* widget,
	const QRectF& bounds)
{
	const qreal dpr = widget->devicePixelRatioF();
	const qreal pixel = 1.0 / dpr;
	const qreal enabledFactor = widget->isEnabled() ? 1.0 : 0.45;
	const QPalette palette = widget->palette();

	const QRectF frame = bounds.adjusted(
		pixel / 2.0, pixel / 2.0, -pixel / 2.0, -pixel / 2.0);

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withOpacity(
		palette.color(QPalette::Base), 0.58 * enabledFactor));
	painter.drawRoundedRect(frame, 8.0, 8.0);

	QPen borderPen(withOpacity(
		palette.color(QPalette::Mid), 0.72 * enabledFactor), pixel);
	borderPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(borderPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame, 8.0, 8.0);

	const qreal reflectionY =
		devicePixelCenter(frame.top() + pixel, dpr);
	QPen reflectionPen(withOpacity(
		palette.color(QPalette::Light), 0.30 * enabledFactor), pixel);
	reflectionPen.setCapStyle(Qt::FlatCap);
	painter.setPen(reflectionPen);
	painter.drawLine(
		QPointF(frame.left() + 8.0, reflectionY),
		QPointF(frame.right() - 8.0, reflectionY));

	const qreal shadeY =
		devicePixelCenter(frame.bottom() - pixel, dpr);
	QPen shadePen(withOpacity(
		palette.color(QPalette::Dark), 0.24 * enabledFactor), pixel);
	shadePen.setCapStyle(Qt::FlatCap);
	painter.setPen(shadePen);
	painter.drawLine(
		QPointF(frame.left() + 8.0, shadeY),
		QPointF(frame.right() - 8.0, shadeY));
}

void drawGlowPath(QPainter& painter, const QPainterPath& path,
	const QColor& color, bool luminous, qreal intensity = 1.0)
{
	intensity = std::clamp(intensity, 0.0, 1.0);

	if (luminous)
	{
		QPen outerPen(withOpacity(color, 0.10 * intensity), 8.0);
		outerPen.setCapStyle(Qt::RoundCap);
		outerPen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(outerPen);
		painter.drawPath(path);

		QPen middlePen(withOpacity(color, 0.25 * intensity), 4.0);
		middlePen.setCapStyle(Qt::RoundCap);
		middlePen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(middlePen);
		painter.drawPath(path);
	}

	QPen corePen(withOpacity(color,
		luminous ? 0.96 * intensity : 0.66 * intensity),
		luminous ? 1.5 : 1.0);
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
		numberText.replace(QLatin1Char(','), QLatin1Char('.'));

		bool ok = false;
		double frequency = numberText.toDouble(&ok);
		if (!ok)
			return 0.0;

		if (!frequencyMatch.captured(2).isEmpty())
			frequency *= 1000.0;

		return std::clamp(frequency, 10.0, 40000.0);
	}

	static const QRegularExpression numberExpression(
		QStringLiteral(R"((\d+(?:[.,]\d+)?))"));

	double largestNumber = 0.0;
	QRegularExpressionMatchIterator matches =
		numberExpression.globalMatch(text);
	while (matches.hasNext())
	{
		QString numberText = matches.next().captured(1);
		numberText.replace(QLatin1Char(','), QLatin1Char('.'));

		bool ok = false;
		const double number = numberText.toDouble(&ok);
		if (ok)
			largestNumber = std::max(largestNumber, number);
	}

	if (largestNumber <= 0.0)
		return 0.0;

	return std::clamp(largestNumber, 10.0, 40000.0);
}

class GlassChipLabel : public QLabel
{
public:
	explicit GlassChipLabel(QWidget* parent)
		: QLabel(parent)
	{
		setAlignment(Qt::AlignCenter);
		setContentsMargins(9, 2, 9, 2);
		setFont(studioMonoFont(QFont::Medium));
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		return QSize(metrics.horizontalAdvance(text()) + 18,
			std::max(24, metrics.height() + 6));
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		paintGlassSurface(painter, this, QRectF(rect()));

		const QString severity =
			property("severity").toString();
		QColor ink = palette().color(QPalette::Text);

		if (severity == QStringLiteral("valid"))
		{
			ink = palette().color(QPalette::Highlight);

			painter.setRenderHint(QPainter::Antialiasing, true);
			painter.setPen(Qt::NoPen);
			painter.setBrush(withOpacity(ink,
				isEnabled() ? 0.09 : 0.0));
			const qreal pixel = 1.0 / devicePixelRatioF();
			painter.drawRoundedRect(
				QRectF(rect()).adjusted(
					pixel, pixel, -pixel, -pixel),
				8.0, 8.0);
		}
		else if (severity == QStringLiteral("invalid"))
		{
			ink = palette().color(QPalette::BrightText);
		}
		else if (severity == QStringLiteral("warning"))
		{
			ink = palette().color(QPalette::Text);
		}
		else
		{
			ink = palette().color(QPalette::Mid);
		}

		if (!isEnabled())
			ink = withOpacity(ink, 0.45);

		painter.setFont(font());
		painter.setPen(ink);
		painter.drawText(
			rect().adjusted(9, 2, -9, -2),
			alignment(),
			fontMetrics().elidedText(
				text(), Qt::ElideRight,
				std::max(0, width() - 18)));
	}
};

class GlowReadoutLabel : public QLabel
{
public:
	explicit GlowReadoutLabel(QWidget* parent)
		: QLabel(parent)
	{
		setAlignment(Qt::AlignCenter);
		setFont(studioMonoFont(QFont::DemiBold));
		setMinimumSize(144, 40);
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		setAttribute(Qt::WA_TransparentForMouseEvents);
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
			std::max(144, metrics.horizontalAdvance(text()) + 24),
			std::max(40, metrics.height() + 16));
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		paintGlassSurface(painter, this, QRectF(rect()));
		painter.setRenderHint(QPainter::Antialiasing, true);

		const QFontMetricsF metrics(font());
		const qreal textWidth = metrics.horizontalAdvance(text());
		const qreal x = (width() - textWidth) / 2.0;
		const qreal y = (height() - metrics.height()) / 2.0
			+ metrics.ascent();

		QPainterPath glyphs;
		glyphs.addText(QPointF(x, y), font(), text());

		if (autoMode && isEnabled())
		{
			const QColor accent =
				palette().color(QPalette::Highlight);

			painter.setBrush(Qt::NoBrush);

			QPen outerGlow(withOpacity(accent, 0.15), 6.0);
			outerGlow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(outerGlow);
			painter.drawPath(glyphs);

			QPen innerGlow(withOpacity(accent, 0.36), 2.5);
			innerGlow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(innerGlow);
			painter.drawPath(glyphs);

			painter.fillPath(glyphs,
				withOpacity(accent, 0.98));
		}
		else
		{
			QColor ink = palette().color(QPalette::Text);
			if (!isEnabled())
				ink = withOpacity(ink, 0.45);
			painter.fillPath(glyphs, ink);
		}
	}

private:
	bool autoMode = false;
};

class BassInstrumentWidget : public QWidget
{
public:
	explicit BassInstrumentWidget(QWidget* parent)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("StudioBassInstrument"));
		setMinimumHeight(78);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	void setPresentation(bool stateValid,
		bool sourceLfeIsPreserved,
		double sourceLfeGain,
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
		sourceLfeGainDb = sourceLfeGain;
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
		return QSize(430, 82);
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		paintGlassSurface(painter, this, QRectF(rect()));
		painter.setRenderHint(QPainter::Antialiasing, true);

		const qreal dpr = devicePixelRatioF();
		const qreal pixel = 1.0 / dpr;
		const QRectF bounds =
			QRectF(rect()).adjusted(9.0, 8.0, -9.0, -8.0);
		const qreal splitX =
			bounds.left() + bounds.width() * 0.61;

		const QPalette currentPalette = palette();
		const QColor accent =
			currentPalette.color(QPalette::Highlight);
		const QColor quiet =
			currentPalette.color(QPalette::Mid);
		const QColor text =
			currentPalette.color(QPalette::Text);
		const QColor muted =
			currentPalette.color(QPalette::Mid);

		const bool luminous = valid && isEnabled();
		const QColor traceColor = luminous ? accent : quiet;

		const qreal dividerX =
			devicePixelCenter(splitX, dpr);
		QPen dividerPen(withOpacity(quiet,
			isEnabled() ? 0.42 : 0.22), pixel);
		dividerPen.setCapStyle(Qt::FlatCap);
		painter.setPen(dividerPen);
		painter.drawLine(
			QPointF(dividerX, bounds.top() + 4.0),
			QPointF(dividerX, bounds.bottom() - 4.0));

		QFont captionFont = studioMonoFont(QFont::Medium);
		if (captionFont.pointSizeF() > 0.0)
		{
			captionFont.setPointSizeF(
				std::max(7.0,
					captionFont.pointSizeF() - 1.0));
		}
		painter.setFont(captionFont);
		painter.setPen(withOpacity(text,
			isEnabled() ? 0.76 : 0.38));

		const qreal mainY =
			devicePixelCenter(bounds.top() + 18.0, dpr);
		const qreal bassY =
			devicePixelCenter(bounds.bottom() - 20.0, dpr);
		const qreal traceStart = bounds.left() + 53.0;
		const qreal traceEnd =
			std::max(traceStart + 18.0, splitX - 11.0);

		painter.drawText(
			QRectF(bounds.left(), mainY - 10.0, 46.0, 20.0),
			Qt::AlignLeft | Qt::AlignVCenter,
			mainCaption);
		painter.drawText(
			QRectF(bounds.left(), bassY - 10.0, 46.0, 20.0),
			Qt::AlignLeft | Qt::AlignVCenter,
			bassCaption);

		QPainterPath mainTrace;
		mainTrace.moveTo(traceStart, mainY);
		mainTrace.lineTo(traceEnd, mainY);
		drawGlowPath(painter, mainTrace, traceColor,
			luminous, 0.92);

		QPainterPath bassTrace;
		bassTrace.moveTo(traceStart, bassY);
		bassTrace.lineTo(traceEnd, bassY);
		drawGlowPath(painter, bassTrace, traceColor,
			luminous, 0.82);

		const qreal lfeY =
			devicePixelCenter(bounds.bottom() - 3.0, dpr);
		const qreal lfeStart = bounds.left() + 27.0;
		QPainterPath lfeTrace;
		lfeTrace.moveTo(lfeStart, lfeY);
		lfeTrace.cubicTo(
			lfeStart + 13.0, lfeY,
			traceStart - 13.0, bassY,
			traceStart, bassY);

		if (sourceLfePreserved)
		{
			drawGlowPath(painter, lfeTrace, traceColor,
				luminous, 0.74);
		}
		else
		{
			QPen offPen(withOpacity(quiet,
				isEnabled() ? 0.55 : 0.28), pixel);
			offPen.setDashPattern({ 3.0, 3.0 });
			offPen.setCapStyle(Qt::FlatCap);
			painter.setPen(offPen);
			painter.drawPath(lfeTrace);
		}

		painter.setPen(withOpacity(muted,
			isEnabled() ? 0.72 : 0.36));
		painter.drawText(
			QRectF(bounds.left(), lfeY - 8.0, 25.0, 16.0),
			Qt::AlignLeft | Qt::AlignVCenter,
			lfeCaption);

		const QRectF responseBounds(
			splitX + 10.0,
			bounds.top() + 10.0,
			std::max(20.0, bounds.right() - splitX - 16.0),
			std::max(24.0, bounds.height() - 18.0));

		painter.setPen(withOpacity(muted,
			isEnabled() ? 0.72 : 0.36));
		painter.drawText(
			QRectF(responseBounds.left(),
				bounds.top() - 1.0,
				responseBounds.width() / 2.0,
				14.0),
			Qt::AlignLeft | Qt::AlignTop,
			highPassCaption);
		painter.drawText(
			QRectF(responseBounds.center().x(),
				bounds.top() - 1.0,
				responseBounds.width() / 2.0,
				14.0),
			Qt::AlignRight | Qt::AlignTop,
			lowPassCaption);

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
				? std::min(minimumCorner, lowPassFrequency)
				: lowPassFrequency;
			maximumCorner =
				std::max(maximumCorner, lowPassFrequency);
		}

		if (maximumCorner <= 0.0)
			return;

		const double minimumFrequency =
			std::max(10.0, minimumCorner / 6.0);
		const double maximumFrequency =
			std::min(40000.0,
				std::max(500.0, maximumCorner * 6.0));
		const double logarithmicMinimum =
			std::log(minimumFrequency);
		const double logarithmicRange =
			std::log(maximumFrequency)
			- logarithmicMinimum;

		auto buildResponsePath =
			[&responseBounds, logarithmicMinimum,
				logarithmicRange](double corner,
				bool highPass)
			{
				QPainterPath responsePath;
				constexpr int sampleCount = 64;

				for (int sample = 0;
					sample < sampleCount; ++sample)
				{
					const double position =
						static_cast<double>(sample)
						/ (sampleCount - 1);
					const double frequency =
						std::exp(logarithmicMinimum
							+ position
							* logarithmicRange);
					const double ratio =
						frequency / corner;
					const double fourthPower =
						std::pow(ratio, 4.0);
					const double magnitude = highPass
						? std::sqrt(fourthPower
							/ (1.0 + fourthPower))
						: 1.0
							/ std::sqrt(
								1.0 + fourthPower);
					const double decibels =
						20.0 * std::log10(
							std::max(0.0316227766,
								magnitude));
					const double normalized =
						std::clamp(
							(decibels + 30.0) / 30.0,
							0.0, 1.0);
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
			responseBounds.adjusted(-8.0, -8.0, 8.0, 8.0));

		if (highPassFrequency > 0.0)
		{
			drawGlowPath(painter,
				buildResponsePath(
					highPassFrequency, true),
				traceColor, luminous, 0.92);
		}

		if (lowPassFrequency > 0.0)
		{
			drawGlowPath(painter,
				buildResponsePath(
					lowPassFrequency, false),
				traceColor, luminous, 0.68);
		}

		painter.restore();
	}

private:
	bool valid = false;
	bool sourceLfePreserved = false;
	double sourceLfeGainDb = 0.0;
	double highPassFrequency = 0.0;
	double lowPassFrequency = 0.0;
	QString mainCaption;
	QString bassCaption;
	QString lfeCaption;
	QString highPassCaption;
	QString lowPassCaption;
};

QString countText(int count, const QString& singular,
	const QString& plural)
{
	return count == 1
		? singular
		: plural.arg(count);
}
}

StudioBassManagementCardView::StudioBassManagementCardView(
	QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(
		QStringLiteral("StudioBassManagementCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 0, 0, 0);
	root->setSpacing(5);

	QHBoxLayout* captionLayout = new QHBoxLayout();
	captionLayout->setContentsMargins(0, 0, 0, 0);
	captionLayout->setSpacing(8);

	validityChip = new GlassChipLabel(this);
	validityChip->setObjectName(
		QStringLiteral("StudioBassValidity"));
	validityChip->setAccessibleName(
		tr("Bass-management validity"));
	validityChip->setToolTip(
		tr("Validation status of the bass-management state"));
	captionLayout->addWidget(
		validityChip, 0, Qt::AlignVCenter);

	layoutLabel = new ElidedLabel(this);
	layoutLabel->setObjectName(
		QStringLiteral("StudioBassLayout"));
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	layoutLabel->setToolTip(
		tr("Physical speaker layout stored in this state"));
	layoutLabel->setMinimumWidth(110);
	layoutLabel->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Preferred);
	layoutLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	captionLayout->addWidget(
		layoutLabel, 2, Qt::AlignVCenter);

	profileLabel = new ElidedLabel(this);
	profileLabel->setObjectName(
		QStringLiteral("StudioBassProfile"));
	profileLabel->setAccessibleName(
		tr("Bass-management profile"));
	profileLabel->setToolTip(
		tr("Embedded state or linked profile name"));
	profileLabel->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Preferred);
	profileLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	captionLayout->addWidget(
		profileLabel, 1, Qt::AlignVCenter);

	captionLayout->addStretch(1);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	captionLayout->addLayout(actionLayout);

	root->addLayout(captionLayout);

	crossoverLabel = new ElidedLabel(this);
	crossoverLabel->setObjectName(
		QStringLiteral("StudioBassCrossovers"));
	crossoverLabel->setAccessibleName(
		tr("Representative crossovers"));
	crossoverLabel->setToolTip(
		tr("Representative high-pass and low-pass crossover sections"));
	crossoverLabel->setFont(
		studioMonoFont(QFont::Medium));
	crossoverLabel->setMinimumWidth(100);
	crossoverLabel->setSizePolicy(
		QSizePolicy::Expanding, QSizePolicy::Preferred);
	crossoverLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
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

	secondaryRow = new QWidget(this);
	secondaryRow->setObjectName(
		QStringLiteral("StudioBassSecondaryRow"));
	secondaryRow->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	QHBoxLayout* secondaryLayout =
		new QHBoxLayout(secondaryRow);
	secondaryLayout->setContentsMargins(0, 0, 0, 0);
	secondaryLayout->setSpacing(6);

	groupChip = new GlassChipLabel(secondaryRow);
	groupChip->setObjectName(
		QStringLiteral("StudioBassCountChip"));
	groupChip->setAccessibleName(
		tr("Speaker group count"));
	groupChip->setToolTip(
		tr("Number of speaker groups"));
	secondaryLayout->addWidget(
		groupChip, 0, Qt::AlignVCenter);

	pathChip = new GlassChipLabel(secondaryRow);
	pathChip->setObjectName(
		QStringLiteral("StudioBassCountChip"));
	pathChip->setAccessibleName(
		tr("Bass path count"));
	pathChip->setToolTip(
		tr("Number of bass paths"));
	secondaryLayout->addWidget(
		pathChip, 0, Qt::AlignVCenter);

	routesChip = new GlassChipLabel(secondaryRow);
	routesChip->setObjectName(
		QStringLiteral("StudioBassCountChip"));
	routesChip->setAccessibleName(
		tr("Active matrix route count"));
	routesChip->setToolTip(
		tr("Number of active matrix routes"));
	secondaryLayout->addWidget(
		routesChip, 0, Qt::AlignVCenter);

	secondaryLayout->addStretch(1);

	lfeLabel = new QLabel(secondaryRow);
	lfeLabel->setObjectName(
		QStringLiteral("StudioBassLfe"));
	lfeLabel->setAccessibleName(
		tr("Source LFE routing"));
	lfeLabel->setToolTip(
		tr("Whether source LFE is preserved and at what gain"));
	lfeLabel->setFont(studioMonoFont());
	lfeLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	secondaryLayout->addWidget(
		lfeLabel, 0, Qt::AlignVCenter);

	root->addWidget(secondaryRow);

	QHBoxLayout* instrumentLayout = new QHBoxLayout();
	instrumentLayout->setContentsMargins(0, 0, 0, 0);
	instrumentLayout->setSpacing(8);

	instrumentPane = new BassInstrumentWidget(this);
	instrumentPane->setAccessibleName(
		tr("Bass-management signal and response display"));
	instrumentPane->setToolTip(
		tr("Main and bass signal traces with representative crossover responses"));
	instrumentLayout->addWidget(instrumentPane, 1);

	headroomPane = new QWidget(this);
	headroomPane->setObjectName(
		QStringLiteral("StudioBassHeadroomPane"));
	headroomPane->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	QVBoxLayout* headroomLayout =
		new QVBoxLayout(headroomPane);
	headroomLayout->setContentsMargins(0, 0, 0, 0);
	headroomLayout->setSpacing(3);

	QLabel* headroomCaption = new QLabel(
		tr("HEADROOM"), headroomPane);
	headroomCaption->setObjectName(
		QStringLiteral("StudioBassHeadroomCaption"));
	headroomCaption->setAlignment(Qt::AlignCenter);
	headroomCaption->setAccessibleName(
		tr("Headroom"));
	headroomCaption->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	headroomLayout->addWidget(headroomCaption);

	headroomReadout =
		new GlowReadoutLabel(headroomPane);
	headroomReadout->setObjectName(
		QStringLiteral("StudioBassHeadroomReadout"));
	headroomReadout->setAccessibleName(
		tr("Headroom trim"));
	headroomReadout->setToolTip(
		tr("Automatic or manual headroom trim"));
	headroomLayout->addWidget(headroomReadout);

	instrumentLayout->addWidget(
		headroomPane, 0, Qt::AlignVCenter);
	root->addLayout(instrumentLayout);

	connect(SkinManager::instance(),
		&SkinManager::skinChanged,
		this,
		[this]()
		{
			update();
			validityChip->update();
			groupChip->update();
			pathChip->update();
			routesChip->update();
			instrumentPane->update();
			headroomReadout->update();
		});

	applyState(state());
	updateResponsiveVisibility(width());
}

void StudioBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(this);
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

	if (QToolButton* toolButton =
		qobject_cast<QToolButton*>(button))
	{
		toolButton->setToolButtonStyle(
			toolButton->text().isEmpty()
				? Qt::ToolButtonIconOnly
				: Qt::ToolButtonTextBesideIcon);
	}

	actionLayout->addWidget(
		button, 0, Qt::AlignVCenter);
	actionButtons.append(button);
}

void StudioBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool linkedProfileMissing =
		state.linkedProfile && state.profileMissing;
	const bool effectiveValid =
		state.valid
		&& state.errorText.isEmpty()
		&& !linkedProfileMissing;
	const bool hasWarning =
		!state.warningText.isEmpty();

	if (!effectiveValid)
	{
		validityChip->setText(tr("! INVALID"));
		validityChip->setProperty(
			"severity", QStringLiteral("invalid"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is invalid"));
	}
	else if (hasWarning)
	{
		validityChip->setText(tr("! WARNING"));
		validityChip->setProperty(
			"severity", QStringLiteral("warning"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is valid with a warning"));
	}
	else
	{
		validityChip->setText(tr("OK VALID"));
		validityChip->setProperty(
			"severity", QStringLiteral("valid"));
		validityChip->setAccessibleDescription(
			tr("The bass-management state is valid"));
	}
	validityChip->update();

	const QString layoutText =
		state.layoutLabel.isEmpty()
			? tr("Unknown")
			: state.layoutLabel;
	layoutLabel->setFullText(
		tr("Layout: %1").arg(layoutText));
	layoutLabel->setToolTip(
		tr("Speaker layout: %1").arg(layoutText));

	QString profileText;
	if (state.linkedProfile)
	{
		const QString profileName =
			state.profileName.isEmpty()
				? tr("Unnamed linked profile")
				: state.profileName;

		profileText = linkedProfileMissing
			? tr("Profile: %1 (linked, missing)")
				.arg(profileName)
			: tr("Profile: %1 (linked)")
				.arg(profileName);
	}
	else
	{
		const QString profileName =
			state.profileName.isEmpty()
				? tr("Embedded state")
				: state.profileName;
		profileText =
			tr("Profile: %1").arg(profileName);
	}
	profileLabel->setFullText(profileText);
	profileLabel->setToolTip(profileText);

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

	groupChip->setText(countText(
		state.speakerGroupCount,
		tr("1 group"),
		tr("%1 groups")));
	groupChip->setAccessibleDescription(
		tr("%1 speaker groups")
			.arg(state.speakerGroupCount));

	pathChip->setText(countText(
		state.bassPathCount,
		tr("1 bass path"),
		tr("%1 bass paths")));
	pathChip->setAccessibleDescription(
		tr("%1 bass paths")
			.arg(state.bassPathCount));

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
					'f', 1))
			: tr("LFE preserved");
	}
	else
	{
		lfeText = tr("LFE not preserved");
	}
	lfeLabel->setText(lfeText);
	lfeLabel->setAccessibleDescription(lfeText);

	QString headroomText;
	QString headroomDescription;
	if (state.headroomAuto)
	{
		if (std::isfinite(state.headroomTrimDb))
		{
			headroomText = tr("AUTO  %1 dB").arg(
				QString::number(
					state.headroomTrimDb,
					'f', 1));
			headroomDescription =
				tr("Automatic headroom trim, %1 decibels")
					.arg(QString::number(
						state.headroomTrimDb,
						'f', 1));
		}
		else
		{
			headroomText = tr("AUTO  -- dB");
			headroomDescription =
				tr("Automatic headroom trim is unavailable");
		}
	}
	else
	{
		if (std::isfinite(state.headroomTrimDb))
		{
			headroomText = tr("MANUAL  %1 dB").arg(
				QString::number(
					state.headroomTrimDb,
					'f', 1));
			headroomDescription =
				tr("Manual headroom trim, %1 decibels")
					.arg(QString::number(
						state.headroomTrimDb,
						'f', 1));
		}
		else
		{
			headroomText = tr("MANUAL  -- dB");
			headroomDescription =
				tr("Manual headroom trim is unavailable");
		}
	}

	headroomReadout->setText(headroomText);
	headroomReadout->setAccessibleDescription(
		headroomDescription);
	static_cast<GlowReadoutLabel*>(
		headroomReadout)->setAutomatic(
			state.headroomAuto);

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
			.arg(highPassDescription,
				lowPassDescription,
				lfeDescription);

	static_cast<BassInstrumentWidget*>(
		instrumentPane)->setPresentation(
			effectiveValid,
			state.sourceLfePreserved,
			state.sourceLfeGainDb,
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
	statusLabel->setProperty("severity",
		!effectiveValid
			? QStringLiteral("invalid")
			: hasWarning
				? QStringLiteral("warning")
				: QStringLiteral("none"));
	statusLabel->update();

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

	for (QAbstractButton* button : actionButtons)
	{
		if (QToolButton* toolButton =
			qobject_cast<QToolButton*>(button))
		{
			toolButton->setToolButtonStyle(
				toolButton->text().isEmpty()
					? Qt::ToolButtonIconOnly
					: Qt::ToolButtonTextBesideIcon);
		}
	}

	updateResponsiveVisibility(width());
}

void StudioBassManagementCardView::paintEvent(
	QPaintEvent* event)
{
	QWidget::paintEvent(event);

	if (!hasFocus())
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const qreal dpr = devicePixelRatioF();
	const qreal pixel = 1.0 / dpr;
	const QRectF focusRect = QRectF(rect()).adjusted(
		pixel / 2.0, pixel / 2.0,
		-pixel / 2.0, -pixel / 2.0);
	const QColor focusColor =
		palette().color(QPalette::Highlight);

	QPen glowPen(withOpacity(focusColor, 0.22), 4.0);
	glowPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(glowPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(focusRect, 8.0, 8.0);

	QPen corePen(withOpacity(focusColor, 0.82), pixel);
	corePen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(corePen);
	painter.drawRoundedRect(focusRect, 8.0, 8.0);
}

void StudioBassManagementCardView::resizeEvent(
	QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	updateResponsiveVisibility(event->size().width());
}

void StudioBassManagementCardView::updateResponsiveVisibility(
	int availableWidth)
{
	profileLabel->setVisible(availableWidth >= 640);
	secondaryRow->setVisible(availableWidth >= 560);
	routesChip->setVisible(availableWidth >= 760);
	lfeLabel->setVisible(availableWidth >= 680);
	instrumentPane->setVisible(availableWidth >= 560);
	headroomPane->setVisible(availableWidth >= 720);
}
