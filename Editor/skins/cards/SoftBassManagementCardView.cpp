/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#include "SoftBassManagementCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/SkinPaint.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
QColor opaqueColor(QColor color)
{
	color.setAlpha(255);
	return color;
}

QColor warmChipInk(const SkinTokens& tokens)
{
	return opaqueColor(skinIsDark(tokens)
		? QColor(tokens.background)
		: QColor(tokens.text));
}

QRectF crispOnePixelRect(const QRectF& rect, qreal devicePixelRatio)
{
	if (devicePixelRatio <= 0.0)
		return rect;

	const qreal left = (std::floor(rect.left() * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
	const qreal top = (std::floor(rect.top() * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
	const qreal right = (std::ceil(rect.right() * devicePixelRatio) - 0.5)
		/ devicePixelRatio;
	const qreal bottom = (std::ceil(rect.bottom() * devicePixelRatio) - 0.5)
		/ devicePixelRatio;

	return QRectF(QPointF(left, top), QPointF(right, bottom));
}

qreal crispLineCoordinate(qreal coordinate, qreal devicePixelRatio)
{
	if (devicePixelRatio <= 0.0)
		return coordinate;

	return (std::floor(coordinate * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
}

void drawRoundedChip(QPainter& painter, const QRectF& rect,
	const QColor& fill, const QColor& border, bool enabled)
{
	const qreal devicePixelRatio = painter.device()->devicePixelRatioF();
	const QRectF paintRect = crispOnePixelRect(rect, devicePixelRatio);

	QPen pen(border, 1.0 / devicePixelRatio);
	pen.setStyle(enabled ? Qt::SolidLine : Qt::DashLine);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);

	painter.setPen(pen);
	painter.setBrush(fill);
	painter.drawRoundedRect(paintRect,
		paintRect.height() / 2.0, paintRect.height() / 2.0);
}

QFont fixedPitchFont(const SkinTokens& tokens, const QFont& fallback)
{
	QFont font(tokens.monoFontFamily);
	if (font.family().isEmpty())
	{
		font = fallback;
		font.setStyleHint(QFont::Monospace);
		font.setFixedPitch(true);
	}
	return font;
}
}

class SoftBassStatusIcon : public QWidget
{
public:
	explicit SoftBassStatusIcon(const QColor& color, QWidget* parent = nullptr)
		: QWidget(parent), ink(color)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFixedSize(GUIHelper::scale(QSize(18, 18)));
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		QColor paintInk = isEnabled()
			? ink
			: QColor(SkinManager::instance()->tokens().mutedText);
		paintInk = opaqueColor(paintInk);

		const qreal devicePixelRatio = devicePixelRatioF();
		QRectF circleRect = rect();
		circleRect.adjust(GUIHelper::scale(1.5), GUIHelper::scale(1.5),
			-GUIHelper::scale(1.5), -GUIHelper::scale(1.5));
		circleRect = crispOnePixelRect(circleRect, devicePixelRatio);

		QPen circlePen(paintInk, 1.0 / devicePixelRatio);
		circlePen.setCapStyle(Qt::RoundCap);
		painter.setPen(circlePen);
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(circleRect);

		const QPointF center = circleRect.center();
		QPen markPen(paintInk, GUIHelper::scale(1.8),
			Qt::SolidLine, Qt::RoundCap);
		painter.setPen(markPen);
		painter.drawLine(
			QPointF(center.x(), circleRect.top() + circleRect.height() * 0.27),
			QPointF(center.x(), circleRect.top() + circleRect.height() * 0.58));

		painter.setPen(Qt::NoPen);
		painter.setBrush(paintInk);
		const qreal dotRadius = GUIHelper::scale(1.0);
		painter.drawEllipse(
			QPointF(center.x(), circleRect.top() + circleRect.height() * 0.75),
			dotRadius, dotRadius);
	}

private:
	QColor ink;
};

class SoftBassStatusChip : public QWidget
{
public:
	SoftBassStatusChip(const QString& objectName, const QColor& fill,
		const QColor& ink, QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(objectName);
		setAttribute(Qt::WA_StyledBackground, true);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

		QHBoxLayout* layout = new QHBoxLayout(this);
		layout->setContentsMargins(GUIHelper::scale(10.0),
			GUIHelper::scale(6.0), GUIHelper::scale(12.0),
			GUIHelper::scale(6.0));
		layout->setSpacing(GUIHelper::scale(7.0));

		SoftBassStatusIcon* icon = new SoftBassStatusIcon(ink, this);
		layout->addWidget(icon, 0, Qt::AlignTop);

		textLabel = new QLabel(this);
		textLabel->setWordWrap(true);
		textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		layout->addWidget(textLabel, 1);

		const SkinTokens& tokens = SkinManager::instance()->tokens();
		const QColor border = opaqueColor(
			mixColor(fill, ink, skinIsDark(tokens) ? 0.20 : 0.14));
		const QColor disabledFill = opaqueColor(
			mixColor(fill, QColor(tokens.background), 0.66));

		const QString selector = QStringLiteral("QWidget#") + objectName;
		setStyleSheet(
			selector + QStringLiteral(
				" { background: %1; border: 1px solid %2;"
				" border-radius: 12px; }")
				.arg(cssColor(fill), cssColor(border))
			+ selector + QStringLiteral(
				" QLabel { color: %1; background: transparent;"
				" font-size: 9pt; font-weight: 600; }")
				.arg(cssColor(ink))
			+ selector + QStringLiteral(
				":disabled { background: %1; border: 1px dashed %2; }")
				.arg(cssColor(disabledFill), tokens.border)
			+ selector + QStringLiteral(
				":disabled QLabel { color: %1; }")
				.arg(tokens.mutedText));
	}

	void setMessage(const QString& message, const QString& accessibleName)
	{
		textLabel->setText(message);
		setAccessibleName(accessibleName);
		setToolTip(message);
	}

private:
	QLabel* textLabel = nullptr;
};

class SoftBassFlowWidget : public QWidget
{
public:
	explicit SoftBassFlowWidget(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("SoftBassFlow"));
		configurePaintOnlyChrome(this);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setMinimumWidth(0);
		setFixedHeight(GUIHelper::scale(52.0));
	}

	void setPresentation(const QString& group, const QString& highPass,
		const QString& bass, const QString& lowPass,
		const QString& destination, const QString& accessibleSummary)
	{
		groupText = group;
		highPassText = highPass;
		bassText = bass;
		lowPassText = lowPass;
		destinationText = destination;
		setAccessibleName(accessibleSummary);
		setToolTip(accessibleSummary);
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		const SkinTokens& tokens = SkinManager::instance()->tokens();
		const bool dark = skinIsDark(tokens);
		const bool enabled = isEnabled();
		const qreal devicePixelRatio = devicePixelRatioF();

		QColor groupFill = opaqueColor(
			softPastelize(QColor(tokens.accent), dark));
		QColor bassFill = opaqueColor(
			softPastelize(QColor(tokens.accent2), dark));
		QColor destinationFill = opaqueColor(
			softPastelize(mixColor(QColor(tokens.success),
				QColor(tokens.accent), 0.35), dark));
		QColor ink = warmChipInk(tokens);
		QColor border = opaqueColor(
			mixColor(QColor(tokens.border), ink, dark ? 0.20 : 0.12));

		if (!enabled)
		{
			groupFill = opaqueColor(
				mixColor(groupFill, QColor(tokens.background), 0.68));
			bassFill = opaqueColor(
				mixColor(bassFill, QColor(tokens.background), 0.68));
			destinationFill = opaqueColor(
				mixColor(destinationFill, QColor(tokens.background), 0.68));
			ink = QColor(tokens.mutedText);
			border = QColor(tokens.border);
		}

		const qreal outerMargin = GUIHelper::scale(1.0);
		const qreal availableWidth = qMax<qreal>(
			0.0, width() - outerMargin * 2.0);
		const qreal gap = qBound<qreal>(
			GUIHelper::scale(7.0),
			availableWidth * 0.045,
			GUIHelper::scale(22.0));
		const qreal chipWidth = qMax<qreal>(
			0.0, availableWidth - gap * 2.0);

		const qreal groupWidth = chipWidth * 0.42;
		const qreal bassWidth = chipWidth * 0.30;
		const qreal destinationWidth = qMax<qreal>(
			0.0, chipWidth - groupWidth - bassWidth);

		const qreal centerY = height() / 2.0;
		const qreal groupHeight = GUIHelper::scale(44.0);
		const qreal bassHeight = GUIHelper::scale(34.0);
		const qreal destinationHeight = GUIHelper::scale(34.0);

		const QRectF groupRect(outerMargin,
			centerY - groupHeight / 2.0,
			groupWidth, groupHeight);
		const QRectF bassRect(groupRect.right() + gap,
			centerY - bassHeight / 2.0,
			bassWidth, bassHeight);
		const QRectF destinationRect(bassRect.right() + gap,
			centerY - destinationHeight / 2.0,
			destinationWidth, destinationHeight);

		QColor flowInk = opaqueColor(
			mixColor(QColor(tokens.border), QColor(tokens.mutedText),
				dark ? 0.36 : 0.26));
		if (!enabled)
			flowInk = QColor(tokens.border);

		const qreal lineY = crispLineCoordinate(centerY, devicePixelRatio);
		QPen flowPen(flowInk, GUIHelper::scale(2.0),
			Qt::SolidLine, Qt::RoundCap);
		painter.setPen(flowPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(
			QPointF(groupRect.right(), lineY),
			QPointF(bassRect.left(), lineY));
		painter.drawLine(
			QPointF(bassRect.right(), lineY),
			QPointF(destinationRect.left(), lineY));

		drawRoundedChip(painter, groupRect,
			groupFill, border, enabled);
		drawRoundedChip(painter, bassRect,
			bassFill, border, enabled);
		drawRoundedChip(painter, destinationRect,
			destinationFill, border, enabled);

		QFont bodyFont = font();
		bodyFont.setWeight(QFont::DemiBold);
		QFont monoFont = fixedPitchFont(tokens, font());
		monoFont.setWeight(QFont::DemiBold);
		if (monoFont.pointSizeF() > 7.5)
			monoFont.setPointSizeF(monoFont.pointSizeF() - 1.0);

		painter.setPen(ink);

		const qreal groupPadding = GUIHelper::scale(10.0);
		const qreal cornerHeight = GUIHelper::scale(18.0);
		const QFontMetrics monoMetrics(monoFont);
		const qreal desiredCornerWidth = monoMetrics.horizontalAdvance(
			highPassText) + GUIHelper::scale(12.0);
		const qreal cornerWidth = qBound<qreal>(
			GUIHelper::scale(30.0),
			desiredCornerWidth,
			qMax<qreal>(GUIHelper::scale(30.0),
				groupRect.width() * 0.52));

		QRectF cornerRect(
			groupRect.right() - cornerWidth - GUIHelper::scale(4.0),
			groupRect.top() + GUIHelper::scale(4.0),
			cornerWidth, cornerHeight);
		QColor cornerFill = opaqueColor(
			mixColor(groupFill, ink, dark ? 0.10 : 0.06));
		if (!enabled)
		{
			cornerFill = opaqueColor(
				mixColor(cornerFill, QColor(tokens.background), 0.45));
		}

		painter.setPen(Qt::NoPen);
		painter.setBrush(cornerFill);
		painter.drawRoundedRect(cornerRect,
			cornerRect.height() / 2.0, cornerRect.height() / 2.0);

		QRectF groupTextRect = groupRect.adjusted(
			groupPadding, GUIHelper::scale(5.0),
			-cornerWidth - GUIHelper::scale(7.0),
			-GUIHelper::scale(5.0));
		painter.setFont(bodyFont);
		painter.setPen(ink);
		const QFontMetrics bodyMetrics(bodyFont);
		painter.drawText(groupTextRect,
			Qt::AlignLeft | Qt::AlignVCenter,
			bodyMetrics.elidedText(groupText, Qt::ElideRight,
				qMax(0, qRound(groupTextRect.width()))));

		painter.setFont(monoFont);
		painter.drawText(cornerRect.adjusted(
			GUIHelper::scale(5.0), 0,
			-GUIHelper::scale(5.0), 0),
			Qt::AlignCenter,
			monoMetrics.elidedText(highPassText, Qt::ElideRight,
				qMax(0, qRound(cornerRect.width()
					- GUIHelper::scale(10.0)))));

		QRectF bassTop = bassRect.adjusted(
			GUIHelper::scale(7.0), GUIHelper::scale(2.0),
			-GUIHelper::scale(7.0), -bassRect.height() / 2.0);
		QRectF bassBottom = bassRect.adjusted(
			GUIHelper::scale(7.0), bassRect.height() / 2.0,
			-GUIHelper::scale(7.0), -GUIHelper::scale(2.0));

		painter.setFont(bodyFont);
		painter.drawText(bassTop, Qt::AlignCenter,
			bodyMetrics.elidedText(bassText, Qt::ElideRight,
				qMax(0, qRound(bassTop.width()))));

		painter.setFont(monoFont);
		painter.drawText(bassBottom, Qt::AlignCenter,
			monoMetrics.elidedText(lowPassText, Qt::ElideRight,
				qMax(0, qRound(bassBottom.width()))));

		painter.setFont(bodyFont);
		QRectF destinationTextRect = destinationRect.adjusted(
			GUIHelper::scale(7.0), 0,
			-GUIHelper::scale(7.0), 0);
		painter.drawText(destinationTextRect, Qt::AlignCenter,
			bodyMetrics.elidedText(destinationText, Qt::ElideRight,
				qMax(0, qRound(destinationTextRect.width()))));
	}

private:
	QString groupText;
	QString highPassText;
	QString bassText;
	QString lowPassText;
	QString destinationText;
};

SoftBassManagementCardView::SoftBassManagementCardView(QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(QStringLiteral("SoftBassManagementCard"));
	configurePaintOnlyChrome(this);
	setAttribute(Qt::WA_Hover);
	setAccessibleName(tr("Bass management summary"));
	setToolTip(tr("Bass-management crossover, routing and headroom summary"));

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(tokens);
	const QColor warningFill = opaqueColor(
		softPastelize(QColor(tokens.warning), dark));
	const QColor errorFill = opaqueColor(
		softPastelize(QColor(tokens.danger), dark));
	const QColor chipInk = warmChipInk(tokens);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(
		GUIHelper::scale(14.0), GUIHelper::scale(12.0),
		GUIHelper::scale(14.0), GUIHelper::scale(12.0));
	root->setSpacing(GUIHelper::scale(8.0));

	QWidget* headerRow = new QWidget(this);
	headerRow->setObjectName(QStringLiteral("SoftBassHeader"));
	QHBoxLayout* headerLayout = new QHBoxLayout(headerRow);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(GUIHelper::scale(9.0));

	validityChip = new QLabel(headerRow);
	validityChip->setObjectName(QStringLiteral("SoftBassValidityChip"));
	validityChip->setAttribute(Qt::WA_StyledBackground, true);
	validityChip->setAttribute(Qt::WA_TransparentForMouseEvents);
	validityChip->setAlignment(Qt::AlignCenter);
	validityChip->setAccessibleName(tr("Bass-management validity"));
	headerLayout->addWidget(validityChip, 0, Qt::AlignVCenter);

	layoutLabel = new ElidedLabel(headerRow);
	layoutLabel->setObjectName(QStringLiteral("SoftBassLayout"));
	layoutLabel->setElideMode(Qt::ElideRight);
	layoutLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	layoutLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	layoutLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; background: transparent;"
		" font-size: 11pt; font-weight: 600; }"
		"QLabel:disabled { color: %2; font-weight: 500; }")
		.arg(tokens.text, tokens.mutedText));
	headerLayout->addWidget(layoutLabel, 1, Qt::AlignVCenter);
	root->addWidget(headerRow);

	crossoverLabel = new QLabel(this);
	crossoverLabel->setObjectName(QStringLiteral("SoftBassCrossover"));
	crossoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	crossoverLabel->setAccessibleName(tr("Crossover summary"));
	crossoverLabel->setToolTip(
		tr("Representative high-pass and low-pass crossover sections"));
	crossoverLabel->setFont(fixedPitchFont(tokens, font()));
	crossoverLabel->setStyleSheet(QStringLiteral(
		"QLabel { color: %1; background: transparent;"
		" font-size: 9pt; font-weight: 600; }"
		"QLabel:disabled { color: %2; }")
		.arg(tokens.mutedText, tokens.mutedText));
	root->addWidget(crossoverLabel);

	flowWidget = new SoftBassFlowWidget(this);
	root->addWidget(flowWidget);

	factRow = new QWidget(this);
	factRow->setObjectName(QStringLiteral("SoftBassFacts"));
	QHBoxLayout* factLayout = new QHBoxLayout(factRow);
	factLayout->setContentsMargins(0, 0, 0, 0);
	factLayout->setSpacing(GUIHelper::scale(6.0));

	routeFact = new QLabel(factRow);
	routeFact->setObjectName(QStringLiteral("SoftBassFactChip"));
	routeFact->setAccessibleName(tr("Active matrix routes"));
	routeFact->setToolTip(
		tr("Number of active bass-management matrix routes"));
	routeFact->setFont(fixedPitchFont(tokens, font()));
	styleFactChip(routeFact);
	factLayout->addWidget(routeFact, 0, Qt::AlignVCenter);

	sourceLfeFact = new QLabel(factRow);
	sourceLfeFact->setObjectName(QStringLiteral("SoftBassFactChip"));
	sourceLfeFact->setAccessibleName(tr("Source LFE routing"));
	sourceLfeFact->setToolTip(
		tr("Whether source LFE is preserved and at what gain"));
	sourceLfeFact->setFont(fixedPitchFont(tokens, font()));
	styleFactChip(sourceLfeFact);
	factLayout->addWidget(sourceLfeFact, 0, Qt::AlignVCenter);

	headroomFact = new QLabel(factRow);
	headroomFact->setObjectName(QStringLiteral("SoftBassFactChip"));
	headroomFact->setAccessibleName(tr("Headroom"));
	headroomFact->setToolTip(tr("Automatic or manual headroom trim"));
	headroomFact->setFont(fixedPitchFont(tokens, font()));
	styleFactChip(headroomFact);
	factLayout->addWidget(headroomFact, 0, Qt::AlignVCenter);

	profileFact = new QLabel(factRow);
	profileFact->setObjectName(QStringLiteral("SoftBassProfileChip"));
	profileFact->setAccessibleName(tr("Bass-management profile"));
	profileFact->setToolTip(tr("Embedded state or linked profile name"));
	styleFactChip(profileFact);
	factLayout->addWidget(profileFact, 0, Qt::AlignVCenter);

	factLayout->addStretch(1);
	root->addWidget(factRow);

	errorChip = new SoftBassStatusChip(
		QStringLiteral("SoftBassErrorChip"),
		errorFill, chipInk, this);
	errorChip->setVisible(false);
	root->addWidget(errorChip);

	warningChip = new SoftBassStatusChip(
		QStringLiteral("SoftBassWarningChip"),
		warningFill, chipInk, this);
	warningChip->setVisible(false);
	root->addWidget(warningChip);

	QWidget* actionRow = new QWidget(this);
	actionRow->setObjectName(QStringLiteral("SoftBassActions"));
	actionRow->setAccessibleName(tr("Bass-management actions"));
	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(GUIHelper::scale(6.0));
	actionLayout->addStretch(1);
	root->addWidget(actionRow);

	connect(SkinManager::instance(), &SkinManager::skinChanged,
		this, [this]()
		{
			update();
			if (flowWidget != nullptr)
				flowWidget->update();
		});

	// Qualified on purpose: seeding the initial presentation from the
	// constructor must not dispatch to a further-derived override.
	SoftBassManagementCardView::applyState(state());
}

void SoftBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setParent(this);
	button->setMinimumSize(GUIHelper::scale(QSize(40, 40)));

	if (button->accessibleName().isEmpty())
	{
		button->setAccessibleName(button->text().isEmpty()
			? tr("Bass-management action")
			: button->text());
	}
	if (button->toolTip().isEmpty())
		button->setToolTip(button->accessibleName());

	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
		toolButton->setAutoRaise(false);

	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void SoftBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool valid = state.valid && state.errorText.isEmpty();
	styleValidityChip(valid, !state.errorText.isEmpty());
	validityChip->setText(valid ? tr("Valid") : tr("Invalid"));
	validityChip->setToolTip(valid
		? tr("This bass-management state is valid")
		: tr("This bass-management state needs attention"));

	const QString layout = state.layoutLabel.isEmpty()
		? tr("Unknown layout")
		: state.layoutLabel;
	layoutLabel->setFullText(tr("Layout: %1").arg(layout));
	layoutLabel->setToolTip(
		tr("Physical speaker layout: %1").arg(layout));

	QStringList crossoverParts;
	if (!state.representativeHighPass.isEmpty())
	{
		crossoverParts.append(
			tr("HP %1").arg(state.representativeHighPass));
	}
	if (!state.representativeLowPass.isEmpty())
	{
		crossoverParts.append(
			tr("LP %1").arg(state.representativeLowPass));
	}
	crossoverLabel->setText(crossoverParts.isEmpty()
		? tr("Crossovers: None")
		: tr("Crossovers: %1").arg(
			crossoverParts.join(QStringLiteral(" / "))));

	const QString highPass = state.representativeHighPass.isEmpty()
		? tr("HP none")
		: tr("HP %1").arg(state.representativeHighPass);
	const QString lowPass = state.representativeLowPass.isEmpty()
		? tr("LP none")
		: tr("LP %1").arg(state.representativeLowPass);
	const QString groupText = tr("%1 speaker groups")
		.arg(state.speakerGroupCount);
	const QString bassText = tr("%1 bass paths")
		.arg(state.bassPathCount);
	const QString destination = state.sourceLfePreserved
		? tr("LFE + output")
		: tr("Output");

	flowWidget->setPresentation(
		groupText, highPass, bassText, lowPass, destination,
		tr("%1 flow through %2 to %3, with %4 and %5")
			.arg(groupText, bassText, destination, highPass, lowPass));

	routeFact->setText(tr("%1 routes").arg(state.activeMatrixEdges));

	if (state.sourceLfePreserved)
	{
		sourceLfeFact->setText(std::isfinite(state.sourceLfeGainDb)
			? tr("LFE %1 dB").arg(
				QString::number(state.sourceLfeGainDb, 'f', 1))
			: tr("LFE preserved"));
	}
	else
	{
		sourceLfeFact->setText(tr("LFE not preserved"));
	}

	if (state.headroomAuto)
	{
		headroomFact->setText(std::isfinite(state.headroomTrimDb)
			? tr("Auto %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1))
			: tr("Auto trim unavailable"));
	}
	else
	{
		headroomFact->setText(std::isfinite(state.headroomTrimDb)
			? tr("Manual %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1))
			: tr("Manual trim unavailable"));
	}

	QString profileText;
	if (state.linkedProfile)
	{
		profileText = state.profileName.isEmpty()
			? tr("Linked profile")
			: tr("Linked: %1").arg(state.profileName);
		if (state.profileMissing)
			profileText += tr(" (missing)");
	}
	else
	{
		profileText = state.profileName.isEmpty()
			? tr("Embedded state")
			: state.profileName;
	}
	profileFact->setText(profileText);
	styleFactChip(profileFact, state.profileMissing);
	profileFact->setToolTip(state.profileMissing
		? tr("The linked bass-management profile is missing")
		: tr("Bass-management profile: %1").arg(profileText));

	errorChip->setVisible(!state.errorText.isEmpty());
	if (!state.errorText.isEmpty())
	{
		errorChip->setMessage(
			tr("Error: %1").arg(state.errorText),
			tr("Bass-management error: %1").arg(state.errorText));
	}

	warningChip->setVisible(!state.warningText.isEmpty());
	if (!state.warningText.isEmpty())
	{
		warningChip->setMessage(
			tr("Warning: %1").arg(state.warningText),
			tr("Bass-management warning: %1").arg(state.warningText));
	}

	updateResponsiveVisibility();
	update();
}

bool SoftBassManagementCardView::event(QEvent* event)
{
	const bool repaint =
		event->type() == QEvent::HoverEnter
		|| event->type() == QEvent::HoverLeave
		|| event->type() == QEvent::FocusIn
		|| event->type() == QEvent::FocusOut
		|| event->type() == QEvent::EnabledChange;

	const bool result = BassManagementCardView::event(event);
	if (repaint)
		update();
	return result;
}

void SoftBassManagementCardView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event)

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QColor fill = QColor(tokens.card);
	QColor border = QColor(tokens.border);

	if (!isEnabled())
	{
		fill = opaqueColor(
			mixColor(fill, QColor(tokens.background), 0.68));
	}
	else if (underMouse())
	{
		fill = QColor(tokens.cardHover);
	}

	const qreal devicePixelRatio = devicePixelRatioF();
	QRectF cardRect = rect();
	cardRect.adjust(
		GUIHelper::scale(0.5), GUIHelper::scale(0.5),
		-GUIHelper::scale(0.5), -GUIHelper::scale(0.5));
	cardRect = crispOnePixelRect(cardRect, devicePixelRatio);

	QPen cardPen(border, 1.0 / devicePixelRatio);
	cardPen.setStyle(isEnabled() ? Qt::SolidLine : Qt::DashLine);
	cardPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(cardPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(cardRect,
		GUIHelper::scale(14.0), GUIHelper::scale(14.0));

	if (hasFocus() && isEnabled())
	{
		QColor focusColor = withAlpha(
			QColor(tokens.accent), 90);
		QRectF focusRect = cardRect.adjusted(
			GUIHelper::scale(2.0), GUIHelper::scale(2.0),
			-GUIHelper::scale(2.0), -GUIHelper::scale(2.0));

		QPen focusPen(focusColor, GUIHelper::scale(3.0));
		focusPen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(focusPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(focusRect,
			GUIHelper::scale(12.0), GUIHelper::scale(12.0));
	}
}

void SoftBassManagementCardView::resizeEvent(QResizeEvent* event)
{
	BassManagementCardView::resizeEvent(event);
	updateResponsiveVisibility();
}

void SoftBassManagementCardView::styleFactChip(
	QLabel* label, bool warning)
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(tokens);
	const QColor seed = warning
		? QColor(tokens.warning)
		: mixColor(QColor(tokens.accent),
			QColor(tokens.mutedText), 0.55);
	const QColor fill = opaqueColor(softPastelize(seed, dark));
	const QColor ink = warmChipInk(tokens);
	const QColor border = opaqueColor(
		mixColor(fill, ink, dark ? 0.18 : 0.10));
	const QColor disabledFill = opaqueColor(
		mixColor(fill, QColor(tokens.background), 0.68));

	label->setAttribute(Qt::WA_StyledBackground, true);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setStyleSheet(QStringLiteral(
		"QLabel { background: %1; color: %2; border: 1px solid %3;"
		" border-radius: 10px; padding: 3px 10px;"
		" font-size: 8pt; font-weight: 600; }"
		"QLabel:disabled { background: %4; color: %5;"
		" border: 1px dashed %6; }")
		.arg(cssColor(fill), cssColor(ink), cssColor(border),
			cssColor(disabledFill), tokens.mutedText, tokens.border));
}

void SoftBassManagementCardView::styleValidityChip(
	bool valid, bool hasError)
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const bool dark = skinIsDark(tokens);
	const QColor seed = valid
		? QColor(tokens.success)
		: QColor(hasError ? tokens.danger : tokens.warning);
	const QColor fill = opaqueColor(softPastelize(seed, dark));
	const QColor ink = warmChipInk(tokens);
	const QColor disabledFill = opaqueColor(
		mixColor(fill, QColor(tokens.background), 0.68));

	validityChip->setStyleSheet(QStringLiteral(
		"QLabel { background: %1; color: %2; border: 0;"
		" border-radius: 11px; padding: 4px 11px;"
		" font-size: 8pt; font-weight: 700; }"
		"QLabel:disabled { background: %3; color: %4;"
		" border: 1px dashed %5; }")
		.arg(cssColor(fill), cssColor(ink),
			cssColor(disabledFill), tokens.mutedText, tokens.border));
}

void SoftBassManagementCardView::updateResponsiveVisibility()
{
	const int currentWidth = width();

	const bool showFacts =
		currentWidth >= GUIHelper::scale(520.0);
	factRow->setVisible(showFacts);

	if (!showFacts)
		return;

	routeFact->setVisible(true);
	sourceLfeFact->setVisible(
		currentWidth >= GUIHelper::scale(640.0));
	headroomFact->setVisible(
		currentWidth >= GUIHelper::scale(720.0));
	profileFact->setVisible(
		currentWidth >= GUIHelper::scale(820.0));
}
