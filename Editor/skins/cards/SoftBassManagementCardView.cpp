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
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
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

void refreshStyle(QWidget* widget)
{
	if (widget == nullptr || widget->style() == nullptr)
		return;

	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

QString formattedDb(double value)
{
	if (!std::isfinite(value))
		return QStringLiteral("--");

	return QStringLiteral("%1 dB").arg(
		QString::number(value, 'f', 1));
}
}

enum class SoftBassStatusKind
{
	Warning,
	Error
};

class SoftBassStatusIcon : public QWidget
{
public:
	explicit SoftBassStatusIcon(
		SoftBassStatusKind kind, QWidget* parent = nullptr)
		: QWidget(parent), statusKind(kind)
	{
		configurePaintOnlyChrome(this);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFixedSize(GUIHelper::scale(QSize(18, 18)));
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);

		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QColor ink = isEnabled()
			? warmChipInk(tokens)
			: opaqueColor(QColor(tokens.mutedText));

		const qreal devicePixelRatio = devicePixelRatioF();
		QRectF circleRect = rect();
		circleRect.adjust(
			GUIHelper::scale(1.5), GUIHelper::scale(1.5),
			-GUIHelper::scale(1.5), -GUIHelper::scale(1.5));
		circleRect = crispOnePixelRect(circleRect, devicePixelRatio);

		QPen circlePen(ink, 1.0 / devicePixelRatio);
		circlePen.setCapStyle(Qt::RoundCap);
		painter.setPen(circlePen);
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(circleRect);

		const QPointF center = circleRect.center();
		QPen markPen(
			ink, GUIHelper::scale(1.8),
			Qt::SolidLine, Qt::RoundCap);
		painter.setPen(markPen);
		painter.drawLine(
			QPointF(
				center.x(),
				circleRect.top() + circleRect.height() * 0.27),
			QPointF(
				center.x(),
				circleRect.top() + circleRect.height() * 0.58));

		painter.setPen(Qt::NoPen);
		painter.setBrush(ink);
		const qreal dotRadius = GUIHelper::scale(1.0);
		painter.drawEllipse(
			QPointF(
				center.x(),
				circleRect.top() + circleRect.height() * 0.75),
			dotRadius, dotRadius);
	}

private:
	SoftBassStatusKind statusKind;
};

class SoftBassStatusChip : public QWidget
{
public:
	explicit SoftBassStatusChip(
		SoftBassStatusKind kind, QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(kind == SoftBassStatusKind::Error
			? QStringLiteral("SoftBassErrorChip")
			: QStringLiteral("SoftBassWarningChip"));
		setAttribute(Qt::WA_StyledBackground, true);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

		QHBoxLayout* layout = new QHBoxLayout(this);
		layout->setContentsMargins(
			GUIHelper::scale(10.0), GUIHelper::scale(6.0),
			GUIHelper::scale(12.0), GUIHelper::scale(6.0));
		layout->setSpacing(GUIHelper::scale(7.0));

		statusIcon = new SoftBassStatusIcon(kind, this);
		layout->addWidget(statusIcon, 0, Qt::AlignTop);

		textLabel = new QLabel(this);
		textLabel->setObjectName(QStringLiteral("SoftBassStatusText"));
		textLabel->setWordWrap(true);
		textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		textLabel->setSizePolicy(
			QSizePolicy::Expanding, QSizePolicy::Minimum);
		layout->addWidget(textLabel, 1);
	}

	void setMessage(const QString& message, const QString& accessibleName)
	{
		textLabel->setText(message);
		textLabel->setToolTip(message);
		setAccessibleName(accessibleName);
		setToolTip(message);
	}

	void refresh()
	{
		update();
		statusIcon->update();
	}

private:
	SoftBassStatusIcon* statusIcon = nullptr;
	QLabel* textLabel = nullptr;
};

// Fully painted on purpose. The pastel fill and the warm ink are Soft's
// badge grammar (softPastelize + warmChipInk); painting keeps the pair
// atomic and immune to sheet cascade surprises, exactly like the skin's
// type badges.
class SoftBassFlowChip : public QWidget
{
public:
	SoftBassFlowChip(
		const QString& role, bool stacked, QWidget* parent = nullptr)
		: QWidget(parent), flowRole(role), stackedChip(stacked)
	{
		setObjectName(QStringLiteral("SoftBassFlowChip"));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setMinimumWidth(0);
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	}

	void setPresentation(
		const QString& caption, const QString& value = QString())
	{
		captionText = caption;
		valueText = value;

		QString summary = caption;
		if (stackedChip && !value.isEmpty())
			summary = QStringLiteral("%1, %2").arg(caption, value);

		setAccessibleName(summary);
		setToolTip(summary);
		updateGeometry();
		update();
	}

	QSize sizeHint() const override
	{
		const QFontMetrics captionMetrics(captionFont());
		const QFontMetrics valueMetrics(valueFont());
		int textWidth = captionMetrics.horizontalAdvance(captionText);
		if (stackedChip)
			textWidth = qMax(textWidth,
				valueMetrics.horizontalAdvance(valueText));
		return QSize(
			textWidth + GUIHelper::scale(28.0),
			GUIHelper::scale(stackedChip ? 46.0 : 36.0));
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);

		const SkinTokens& tokens = SkinManager::instance()->tokens();
		const bool dark = skinIsDark(tokens);
		const qreal devicePixelRatio = devicePixelRatioF();
		const QRectF frame = crispOnePixelRect(
			QRectF(rect()), devicePixelRatio);
		const qreal radius = frame.height() / 2.0;

		const QString roleToken =
			flowRole == QStringLiteral("bass") ? tokens.accent2
			: flowRole == QStringLiteral("destination") ? tokens.success
			: tokens.accent;

		if (isEnabled())
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(opaqueColor(
				softPastelize(QColor(roleToken), dark)));
			painter.drawRoundedRect(frame, radius, radius);
		}
		else
		{
			QPen sleepPen(opaqueColor(QColor(tokens.border)),
				1.0 / devicePixelRatio, Qt::DashLine);
			painter.setPen(sleepPen);
			painter.setBrush(opaqueColor(QColor(tokens.background)));
			painter.drawRoundedRect(frame, radius, radius);
		}

		const QColor ink = isEnabled()
			? warmChipInk(tokens)
			: opaqueColor(QColor(tokens.mutedText));
		const qreal sidePadding = GUIHelper::scale(12.0);
		const QRectF textArea = frame.adjusted(
			sidePadding, 0.0, -sidePadding, 0.0);

		painter.setPen(ink);
		if (stackedChip)
		{
			const qreal half = textArea.height() / 2.0;
			const QRectF captionArea(
				textArea.left(),
				textArea.top() + GUIHelper::scale(3.0),
				textArea.width(), half - GUIHelper::scale(3.0));
			const QRectF valueArea(
				textArea.left(), textArea.top() + half,
				textArea.width(), half - GUIHelper::scale(3.0));

			painter.setFont(captionFont());
			painter.drawText(captionArea,
				Qt::AlignHCenter | Qt::AlignVCenter,
				QFontMetrics(captionFont()).elidedText(captionText,
					Qt::ElideRight, int(captionArea.width())));

			painter.setFont(valueFont());
			painter.drawText(valueArea,
				Qt::AlignHCenter | Qt::AlignVCenter,
				QFontMetrics(valueFont()).elidedText(valueText,
					Qt::ElideRight, int(valueArea.width())));
		}
		else
		{
			painter.setFont(captionFont());
			painter.drawText(textArea, Qt::AlignCenter,
				QFontMetrics(captionFont()).elidedText(captionText,
					Qt::ElideRight, int(textArea.width())));
		}
	}

private:
	QFont captionFont() const
	{
		QFont font(SkinManager::instance()->tokens().fontFamily);
		font.setPointSizeF(9.0);
		font.setWeight(QFont::DemiBold);
		return font;
	}

	QFont valueFont() const
	{
		QFont font(SkinManager::instance()->tokens().monoFontFamily);
		font.setFixedPitch(true);
		font.setPointSizeF(8.0);
		font.setWeight(QFont::Medium);
		return font;
	}

	QString flowRole;
	bool stackedChip = false;
	QString captionText;
	QString valueText;
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
		setFixedHeight(GUIHelper::scale(58.0));

		flowLayout = new QHBoxLayout(this);
		flowLayout->setContentsMargins(
			GUIHelper::scale(1.0), GUIHelper::scale(2.0),
			GUIHelper::scale(1.0), GUIHelper::scale(2.0));
		flowLayout->setSpacing(GUIHelper::scale(14.0));

		groupChip = new SoftBassFlowChip(
			QStringLiteral("group"), true, this);
		flowLayout->addWidget(groupChip, 42);

		bassChip = new SoftBassFlowChip(
			QStringLiteral("bass"), true, this);
		flowLayout->addWidget(bassChip, 30);

		destinationChip = new SoftBassFlowChip(
			QStringLiteral("destination"), false, this);
		flowLayout->addWidget(destinationChip, 28);
	}

	void setPresentation(
		const QString& group,
		const QString& highPass,
		const QString& bass,
		const QString& lowPass,
		const QString& destination,
		const QString& accessibleSummary)
	{
		groupChip->setPresentation(group, highPass);
		bassChip->setPresentation(bass, lowPass);
		destinationChip->setPresentation(destination);

		setAccessibleName(accessibleSummary);
		setToolTip(accessibleSummary);
		updateResponsiveVisibility();
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, false);

		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QColor lineColor = isEnabled()
			? opaqueColor(mixColor(
				QColor(tokens.border),
				QColor(tokens.mutedText),
				skinIsDark(tokens) ? 0.36 : 0.26))
			: opaqueColor(QColor(tokens.border));

		const qreal devicePixelRatio = devicePixelRatioF();
		QPen pen(lineColor, 1.0 / devicePixelRatio);
		pen.setCapStyle(Qt::FlatCap);
		painter.setPen(pen);

		QList<SoftBassFlowChip*> visibleChips;
		if (groupChip->isVisible())
			visibleChips.append(groupChip);
		if (bassChip->isVisible())
			visibleChips.append(bassChip);
		if (destinationChip->isVisible())
			visibleChips.append(destinationChip);

		for (int index = 0; index + 1 < visibleChips.size(); ++index)
		{
			const QRectF firstRect(visibleChips.at(index)->geometry());
			const QRectF secondRect(
				visibleChips.at(index + 1)->geometry());
			const qreal lineY = crispLineCoordinate(
				(firstRect.center().y() + secondRect.center().y())
					/ 2.0,
				devicePixelRatio);

			painter.drawLine(
				QPointF(firstRect.right(), lineY),
				QPointF(secondRect.left(), lineY));
		}
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		updateResponsiveVisibility();
		update();
	}

private:
	void updateResponsiveVisibility()
	{
		const int currentWidth = width();

		groupChip->setVisible(true);
		bassChip->setVisible(
			currentWidth >= GUIHelper::scale(250.0));
		destinationChip->setVisible(
			currentWidth >= GUIHelper::scale(390.0));
	}

	QHBoxLayout* flowLayout = nullptr;
	SoftBassFlowChip* groupChip = nullptr;
	SoftBassFlowChip* bassChip = nullptr;
	SoftBassFlowChip* destinationChip = nullptr;
};

SoftBassManagementCardView::SoftBassManagementCardView(QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(QStringLiteral("SoftBassManagementCard"));
	configurePaintOnlyChrome(this);
	setAttribute(Qt::WA_Hover);
	setAccessibleName(tr("Bass management summary"));
	setToolTip(
		tr("Bass-management crossover, routing and headroom summary"));

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
	validityChip->setObjectName(
		QStringLiteral("SoftBassValidityChip"));
	validityChip->setAttribute(Qt::WA_StyledBackground, true);
	validityChip->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	validityChip->setAlignment(Qt::AlignCenter);
	validityChip->setAccessibleName(
		tr("Bass-management validity"));
	headerLayout->addWidget(validityChip, 0, Qt::AlignVCenter);

	layoutLabel = new ElidedLabel(headerRow);
	layoutLabel->setObjectName(QStringLiteral("SoftBassLayout"));
	layoutLabel->setElideMode(Qt::ElideRight);
	layoutLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	layoutLabel->setMinimumWidth(0);
	layoutLabel->setSizePolicy(
		QSizePolicy::Ignored, QSizePolicy::Preferred);
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	headerLayout->addWidget(layoutLabel, 1, Qt::AlignVCenter);

	root->addWidget(headerRow);

	crossoverLabel = new ElidedLabel(this);
	crossoverLabel->setObjectName(
		QStringLiteral("SoftBassCrossover"));
	crossoverLabel->setElideMode(Qt::ElideRight);
	crossoverLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	crossoverLabel->setMinimumWidth(0);
	crossoverLabel->setSizePolicy(
		QSizePolicy::Ignored, QSizePolicy::Preferred);
	crossoverLabel->setAccessibleName(tr("Crossover summary"));
	root->addWidget(crossoverLabel);

	flowWidget = new SoftBassFlowWidget(this);
	root->addWidget(flowWidget);

	errorChip = new SoftBassStatusChip(
		SoftBassStatusKind::Error, this);
	errorChip->setVisible(false);
	root->addWidget(errorChip);

	warningChip = new SoftBassStatusChip(
		SoftBassStatusKind::Warning, this);
	warningChip->setVisible(false);
	root->addWidget(warningChip);

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
	styleFactChip(routeFact);
	factLayout->addWidget(routeFact, 0, Qt::AlignVCenter);

	sourceLfeFact = new QLabel(factRow);
	sourceLfeFact->setObjectName(
		QStringLiteral("SoftBassFactChip"));
	sourceLfeFact->setAccessibleName(tr("Source LFE routing"));
	sourceLfeFact->setToolTip(
		tr("Whether source LFE is preserved and at what gain"));
	styleFactChip(sourceLfeFact);
	factLayout->addWidget(sourceLfeFact, 0, Qt::AlignVCenter);

	headroomFact = new QLabel(factRow);
	headroomFact->setObjectName(
		QStringLiteral("SoftBassFactChip"));
	headroomFact->setAccessibleName(tr("Headroom"));
	headroomFact->setToolTip(
		tr("Automatic or manual headroom trim"));
	styleFactChip(headroomFact);
	factLayout->addWidget(headroomFact, 0, Qt::AlignVCenter);

	profileFact = new ElidedLabel(factRow);
	profileFact->setObjectName(
		QStringLiteral("SoftBassProfileChip"));
	profileFact->setElideMode(Qt::ElideRight);
	profileFact->setMinimumWidth(0);
	profileFact->setMaximumWidth(GUIHelper::scale(240.0));
	profileFact->setSizePolicy(
		QSizePolicy::Ignored, QSizePolicy::Preferred);
	profileFact->setAccessibleName(
		tr("Bass-management profile"));
	styleFactChip(profileFact);
	factLayout->addWidget(profileFact, 1, Qt::AlignVCenter);

	factLayout->addStretch(1);
	root->addWidget(factRow);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(GUIHelper::scale(8.0));
	root->addLayout(actionLayout);

	connect(
		SkinManager::instance(), &SkinManager::skinChanged,
		this,
		[this]()
		{
			update();
			flowWidget->update();
			warningChip->refresh();
			errorChip->refresh();
		});

	SoftBassManagementCardView::applyState(state());
}

void SoftBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	const int actionIndex = actionButtons.size();
	const QString fallbackText = actionIndex == 0
		? tr("Open editor")
		: actionIndex == 1
			? tr("Preset")
			: tr("Action");

	button->setParent(this);
	button->setObjectName(
		QStringLiteral("SoftBassActionButton"));
	button->setMinimumSize(GUIHelper::scale(QSize(40, 40)));
	button->setIconSize(GUIHelper::scale(QSize(18, 18)));

	if (button->text().isEmpty())
		button->setText(fallbackText);

	if (button->accessibleName().isEmpty())
		button->setAccessibleName(button->text());

	if (button->toolTip().isEmpty())
		button->setToolTip(button->accessibleName());

	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
	{
		toolButton->setAutoRaise(false);
		toolButton->setToolButtonStyle(
			toolButton->icon().isNull()
				? Qt::ToolButtonTextOnly
				: Qt::ToolButtonTextBesideIcon);
	}

	actionButtons.append(button);
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
	updateResponsiveVisibility();
}

void SoftBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	const bool hasError = !state.errorText.isEmpty();
	const bool valid = state.valid && !hasError;

	styleValidityChip(valid, hasError);
	if (valid)
	{
		validityChip->setText(tr("OK - Valid"));
		validityChip->setToolTip(
			tr("This bass-management state is valid"));
	}
	else if (hasError)
	{
		validityChip->setText(tr("! Error"));
		validityChip->setToolTip(
			tr("This bass-management state has an error"));
	}
	else
	{
		validityChip->setText(tr("! Check"));
		validityChip->setToolTip(
			tr("This bass-management state needs attention"));
	}

	const QString layout = state.layoutLabel.isEmpty()
		? tr("Unknown layout")
		: state.layoutLabel;
	const QString layoutText = tr("Layout: %1").arg(layout);
	layoutLabel->setFullText(layoutText);
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

	const QString crossoverText = crossoverParts.isEmpty()
		? tr("Crossovers: None")
		: tr("Crossovers: %1").arg(
			crossoverParts.join(QStringLiteral(" / ")));
	crossoverLabel->setFullText(crossoverText);
	crossoverLabel->setToolTip(crossoverText);

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
		groupText,
		highPass,
		bassText,
		lowPass,
		destination,
		tr("%1 flow through %2 to %3, with %4 and %5")
			.arg(
				groupText,
				bassText,
				destination,
				highPass,
				lowPass));

	routeFact->setText(
		tr("%1 routes").arg(state.activeMatrixEdges));

	if (state.sourceLfePreserved)
	{
		sourceLfeFact->setText(
			tr("LFE %1").arg(formattedDb(
				state.sourceLfeGainDb)));
	}
	else
	{
		sourceLfeFact->setText(tr("LFE not preserved"));
	}

	headroomFact->setText(state.headroomAuto
		? tr("Auto trim %1").arg(
			formattedDb(state.headroomTrimDb))
		: tr("Manual trim %1").arg(
			formattedDb(state.headroomTrimDb)));

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

	profileFact->setFullText(profileText);
	styleFactChip(profileFact, state.profileMissing);
	profileFact->setToolTip(state.profileMissing
		? tr("Missing bass-management profile: %1")
			.arg(profileText)
		: tr("Bass-management profile: %1")
			.arg(profileText));

	errorChip->setVisible(hasError);
	if (hasError)
	{
		errorChip->setMessage(
			tr("Error: %1").arg(state.errorText),
			tr("Bass-management error: %1")
				.arg(state.errorText));
	}

	const bool hasWarning = !state.warningText.isEmpty();
	warningChip->setVisible(hasWarning);
	if (hasWarning)
	{
		warningChip->setMessage(
			tr("Warning: %1").arg(state.warningText),
			tr("Bass-management warning: %1")
				.arg(state.warningText));
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
	const QColor border = QColor(tokens.border);

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
	cardPen.setStyle(isEnabled()
		? Qt::SolidLine
		: Qt::DashLine);
	cardPen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(cardPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(
		cardRect,
		GUIHelper::scale(14.0),
		GUIHelper::scale(14.0));

	if (hasFocus() && isEnabled())
	{
		const QColor focusColor = withAlpha(
			QColor(tokens.accent), 90);
		const QRectF focusRect = cardRect.adjusted(
			GUIHelper::scale(2.0), GUIHelper::scale(2.0),
			-GUIHelper::scale(2.0), -GUIHelper::scale(2.0));

		QPen focusPen(
			focusColor, GUIHelper::scale(3.0));
		focusPen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(focusPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(
			focusRect,
			GUIHelper::scale(12.0),
			GUIHelper::scale(12.0));
	}
}

void SoftBassManagementCardView::resizeEvent(
	QResizeEvent* event)
{
	BassManagementCardView::resizeEvent(event);
	updateResponsiveVisibility();
}

void SoftBassManagementCardView::styleFactChip(
	QLabel* label, bool warning)
{
	if (label == nullptr)
		return;

	label->setAttribute(Qt::WA_StyledBackground, true);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setProperty(
		"severity",
		warning
			? QStringLiteral("warning")
			: QStringLiteral("normal"));
	refreshStyle(label);
}

void SoftBassManagementCardView::styleValidityChip(
	bool valid, bool hasError)
{
	validityChip->setProperty(
		"severity",
		valid
			? QStringLiteral("valid")
			: hasError
				? QStringLiteral("error")
				: QStringLiteral("warning"));
	refreshStyle(validityChip);
}

void SoftBassManagementCardView::updateResponsiveVisibility()
{
	const int currentWidth = width();
	const bool showButtonText =
		currentWidth >= GUIHelper::scale(430.0);

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
			toolButton->setToolButtonStyle(showButtonText
				? Qt::ToolButtonTextBesideIcon
				: Qt::ToolButtonIconOnly);
		}
	}

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
