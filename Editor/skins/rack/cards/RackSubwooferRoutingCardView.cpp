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
