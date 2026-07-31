/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "BassManagementResponseView.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include "BassManagement/Compiler.h"
#include "Editor/SkinManager.h"
#include "Editor/SkinTokens.h"
#include "Editor/widgets/bassmanagement/BassManagementUiModel.h"

namespace
{
constexpr double kMinimumFrequencyHz = 20.0;
constexpr double kMaximumFrequencyHz = 20000.0;
constexpr int kSampleCount = 256;
constexpr double kMinimumMagnitude = 1.0e-12;

bassmgmt::PrepareSpec prepareSpecFor(
	const bassmgmt::BassManagementState& state,
	double sampleRate)
{
	bassmgmt::PrepareSpec spec;
	spec.sampleRate = sampleRate;
	spec.maximumBlockSize = 1024;
	spec.channelLayout.reserve(state.layout.channels.size());

	for (const bassmgmt::PhysicalChannel& channel
		: state.layout.channels)
	{
		spec.channelLayout.push_back(channel.id);
	}

	return spec;
}

double biquadMagnitude(
	const bassmgmt::BiquadCoefficients& coefficients,
	double frequencyHz,
	double sampleRate)
{
	const double omega =
		2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
	const std::complex<double> z1 =
		std::exp(std::complex<double>(0.0, -omega));
	const std::complex<double> z2 = z1 * z1;

	const std::complex<double> numerator =
		coefficients.b0
		+ coefficients.b1 * z1
		+ coefficients.b2 * z2;
	const std::complex<double> denominator =
		1.0
		+ coefficients.a1 * z1
		+ coefficients.a2 * z2;

	if (std::abs(denominator) <= kMinimumMagnitude)
		return 1.0 / kMinimumMagnitude;

	return std::abs(numerator / denominator);
}

QString frequencyLabel(double frequencyHz)
{
	if (frequencyHz >= 1000.0)
	{
		const double kilohertz = frequencyHz / 1000.0;
		return qFuzzyCompare(kilohertz, std::round(kilohertz))
			? QStringLiteral("%1k").arg(
				static_cast<int>(std::round(kilohertz)))
			: QStringLiteral("%1k").arg(kilohertz, 0, 'g', 2);
	}

	return QString::number(static_cast<int>(std::round(frequencyHz)));
}

QString decibelLabel(double decibels)
{
	const int rounded = static_cast<int>(std::round(decibels));
	return rounded > 0
		? QStringLiteral("+%1").arg(rounded)
		: QString::number(rounded);
}

QColor curveColor(const QColor& tokenColor, bassmgmt::PathKind kind)
{
	int hue = tokenColor.hsvHue();
	if (hue < 0)
		hue = 210;

	int offset = 0;
	switch (kind)
	{
	case bassmgmt::PathKind::Main:
		offset = 0;
		break;
	case bassmgmt::PathKind::Bass:
		offset = 115;
		break;
	case bassmgmt::PathKind::SourceLfe:
		offset = 235;
		break;
	}

	return QColor::fromHsv(
		(hue + offset) % 360,
		std::max(150, tokenColor.hsvSaturation()),
		std::max(175, tokenColor.value()));
}
}

BassManagementResponseView::BassManagementResponseView(
	BassManagementUiModel* uiModel,
	QWidget* parent)
	: QWidget(parent),
	  model(uiModel)
{
	setObjectName(QStringLiteral("BassManagementResponseView"));
	setMinimumHeight(210);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	connect(model, &BassManagementUiModel::stateEdited,
		this, &BassManagementResponseView::recompute);
	connect(SkinManager::instance(), &SkinManager::skinChanged,
		this,
		[this](const SkinTokens&)
		{
			update();
		});

	recompute();
}

QSize BassManagementResponseView::sizeHint() const
{
	return QSize(620, 260);
}

void BassManagementResponseView::recompute()
{
	curves.clear();
	appliedTrimDb.reset();

	const double sampleRate =
		model->sampleRate() > 0 ? model->sampleRate() : 48000.0;
	const bassmgmt::CompileResult compiled = bassmgmt::compile(
		model->state(),
		prepareSpecFor(model->state(), sampleRate));

	if (!compiled.graph.has_value())
	{
		update();
		return;
	}

	if (compiled.headroom.has_value())
		appliedTrimDb = compiled.headroom->appliedTrimDb;

	double sampledMinimum = 0.0;
	double sampledMaximum = 0.0;

	for (const bassmgmt::CompiledPath& path
		: compiled.graph->paths())
	{
		ResponseCurve curve;
		curve.id = QString::fromLatin1(
			path.id.data(), static_cast<int>(path.id.size()));
		curve.kind = path.kind;
		curve.samples.reserve(kSampleCount);

		for (int index = 0; index < kSampleCount; index++)
		{
			const double ratio =
				static_cast<double>(index) / (kSampleCount - 1);
			const double frequencyHz = kMinimumFrequencyHz
				* std::pow(
					kMaximumFrequencyHz / kMinimumFrequencyHz,
					ratio);

			double magnitude = 1.0;
			for (const bassmgmt::CompiledStage& stage : path.stages)
			{
				if (stage.kind != bassmgmt::CompiledStageKind::Biquad)
					continue;

				magnitude *= biquadMagnitude(
					stage.biquad, frequencyHz, sampleRate);
			}

			const double decibels = 20.0 * std::log10(
				std::max(kMinimumMagnitude, magnitude));
			const double bounded = std::clamp(decibels, -120.0, 36.0);
			curve.samples.append(QPointF(frequencyHz, bounded));
			sampledMinimum = std::min(sampledMinimum, bounded);
			sampledMaximum = std::max(sampledMaximum, bounded);
		}

		curves.append(std::move(curve));
	}

	if (appliedTrimDb.has_value())
	{
		sampledMinimum = std::min(sampledMinimum, *appliedTrimDb);
		sampledMaximum = std::max(sampledMaximum, *appliedTrimDb);
	}

	minimumDb = std::min(-24.0, std::floor(sampledMinimum / 6.0) * 6.0);
	maximumDb = std::max(6.0, std::ceil(sampledMaximum / 6.0) * 6.0);
	if (maximumDb - minimumDb < 36.0)
		minimumDb = maximumDb - 36.0;

	update();
}

QRectF BassManagementResponseView::plotRect() const
{
	return QRectF(rect()).adjusted(42.0, 12.0, -12.0, -30.0);
}

double BassManagementResponseView::frequencyToX(
	double frequencyHz) const
{
	const QRectF area = plotRect();
	const double ratio = std::log(
		std::clamp(
			frequencyHz,
			kMinimumFrequencyHz,
			kMaximumFrequencyHz)
		/ kMinimumFrequencyHz)
		/ std::log(kMaximumFrequencyHz / kMinimumFrequencyHz);
	return area.left() + ratio * area.width();
}

double BassManagementResponseView::decibelToY(double decibels) const
{
	const QRectF area = plotRect();
	return area.top()
		+ (maximumDb - decibels)
		/ (maximumDb - minimumDb)
		* area.height();
}

void BassManagementResponseView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor textColor(tokens.text);
	QColor background = textColor;
	background.setAlpha(10);
	painter.fillRect(rect(), background);

	const QRectF area = plotRect();
	QColor borderColor = textColor;
	borderColor.setAlpha(85);
	painter.setPen(QPen(borderColor, 1.0));
	painter.drawRect(area);

	QColor minorGrid = textColor;
	minorGrid.setAlpha(30);
	QColor majorGrid = textColor;
	majorGrid.setAlpha(55);

	const double frequencyGrid[] =
		{20.0, 50.0, 100.0, 200.0, 500.0,
		 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};
	for (const double frequencyHz : frequencyGrid)
	{
		const bool major = frequencyHz == 20.0
			|| frequencyHz == 100.0
			|| frequencyHz == 1000.0
			|| frequencyHz == 10000.0
			|| frequencyHz == 20000.0;
		const double x = frequencyToX(frequencyHz);

		painter.setPen(QPen(major ? majorGrid : minorGrid, 1.0));
		painter.drawLine(QPointF(x, area.top()),
			QPointF(x, area.bottom()));

		if (major)
		{
			painter.setPen(textColor);
			painter.drawText(
				QRectF(x - 24.0, area.bottom() + 4.0, 48.0, 18.0),
				Qt::AlignHCenter | Qt::AlignTop,
				frequencyLabel(frequencyHz));
		}
	}

	const double gridStepDb = 12.0;
	const double firstGridDb =
		std::floor(maximumDb / gridStepDb) * gridStepDb;
	for (double decibels = firstGridDb;
		decibels >= minimumDb;
		decibels -= gridStepDb)
	{
		const double y = decibelToY(decibels);
		painter.setPen(QPen(
			std::abs(decibels) < 0.001 ? majorGrid : minorGrid,
			1.0));
		painter.drawLine(QPointF(area.left(), y),
			QPointF(area.right(), y));

		painter.setPen(textColor);
		painter.drawText(
			QRectF(0.0, y - 9.0, area.left() - 6.0, 18.0),
			Qt::AlignRight | Qt::AlignVCenter,
			decibelLabel(decibels));
	}

	for (const ResponseCurve& curve : curves)
	{
		if (curve.samples.isEmpty())
			continue;

		QPainterPath path;
		path.moveTo(
			frequencyToX(curve.samples.front().x()),
			decibelToY(curve.samples.front().y()));

		for (int index = 1; index < curve.samples.size(); index++)
		{
			path.lineTo(
				frequencyToX(curve.samples[index].x()),
				decibelToY(curve.samples[index].y()));
		}

		painter.setPen(QPen(curveColor(textColor, curve.kind), 1.8));
		painter.drawPath(path);
	}

	if (appliedTrimDb.has_value())
	{
		const double y = decibelToY(*appliedTrimDb);
		QColor trimColor = textColor;
		trimColor.setAlpha(190);

		QPen trimPen(trimColor, 1.2, Qt::DashLine);
		painter.setPen(trimPen);
		painter.drawLine(QPointF(area.left(), y),
			QPointF(area.right(), y));

		const QString label = tr("Headroom %1 dB")
			.arg(QString::number(*appliedTrimDb, 'f', 1));
		const QFontMetrics metrics(font());
		const int width = metrics.horizontalAdvance(label) + 8;
		const QRectF labelRect(
			area.right() - width,
			std::clamp(y - 18.0, area.top(), area.bottom() - 18.0),
			width,
			18.0);

		QColor labelBackground = textColor;
		labelBackground.setAlpha(24);
		painter.fillRect(labelRect, labelBackground);
		painter.setPen(textColor);
		painter.drawText(labelRect.adjusted(4.0, 0.0, -4.0, 0.0),
			Qt::AlignVCenter | Qt::AlignRight, label);
	}

	if (curves.isEmpty())
	{
		painter.setPen(textColor);
		painter.drawText(
			area,
			Qt::AlignCenter | Qt::TextWordWrap,
			tr("The current state could not be compiled."));
		return;
	}

	double legendX = area.left() + 6.0;
	const double legendY = area.top() + 6.0;
	for (const ResponseCurve& curve : curves)
	{
		const QColor color = curveColor(textColor, curve.kind);
		painter.setPen(QPen(color, 2.0));
		painter.drawLine(
			QPointF(legendX, legendY + 6.0),
			QPointF(legendX + 14.0, legendY + 6.0));

		painter.setPen(textColor);
		const int textWidth =
			painter.fontMetrics().horizontalAdvance(curve.id);
		painter.drawText(
			QRectF(legendX + 18.0, legendY, textWidth + 4.0, 14.0),
			Qt::AlignLeft | Qt::AlignVCenter,
			curve.id);
		legendX += textWidth + 32.0;

		if (legendX > area.right() - 80.0)
			break;
	}
}
