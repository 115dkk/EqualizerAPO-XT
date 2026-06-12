/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Default knob rendering shared by every skin. The body is the pre-hook
	AudioKnob::paintEvent moved verbatim behind ISkin::paintKnob, so skins
	that do not override the hook keep exactly the appearance they had before
	the hook existed.
*/

#include "ISkin.h"

#include <QAction>
#include <QPainter>
#include <QToolBar>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FilterPickerView.h"

namespace
{
QPointF pointOnArc(const QRectF& rect, double degrees)
{
	double radians = qDegreesToRadians(degrees);
	QPointF center = rect.center();
	double radius = qMin(rect.width(), rect.height()) / 2.0;
	// Qt measures arc angles counter-clockwise from 3 o'clock, so the matching
	// screen point subtracts sin for Y (screen Y grows downward). The previous
	// +sin mirrored the indicator dot vertically, so it never tracked the value
	// arc and looked like it floated on its own.
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}
}

void ISkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	// Draw inside a centred square so the knob stays circular even when the
	// hosting widget is not square (promoted legacy dials are 100x66).
	QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
	double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
	int spanDegrees = 270;
	int startDegrees = 135;
	double ratio = state.ratio;

	QPen trackPen(QColor(tokens.border), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(trackPen);
	painter.drawArc(knobRect, -startDegrees * 16, -spanDegrees * 16);

	QPen valuePen(QColor(tokens.accent), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(valuePen);
	painter.drawArc(knobRect, -startDegrees * 16, -static_cast<int>(spanDegrees * ratio * 16));

	QColor fill(tokens.card);
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(fill);
	painter.drawEllipse(knobRect.adjusted(6, 6, -6, -6));

	double endDegrees = startDegrees + spanDegrees * ratio;
	QPointF dot = pointOnArc(knobRect.adjusted(3, 3, -3, -3), -endDegrees);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(tokens.accent));
	painter.drawEllipse(dot, 4, 4);

	// Only draw centred text when an explicit value string was supplied (e.g.
	// the Preamp card). Promoted legacy dials drive a separate spin box for the
	// real value and map the dial to log-scaled steps, so painting value() here
	// would show a meaningless step count.
	if (!state.valueText.isEmpty())
	{
		painter.setPen(QColor(tokens.text));
		QFont valueFont = painter.font();
		valueFont.setBold(true);
		valueFont.setPointSizeF(qMax(7.0, valueFont.pointSizeF() - 1.0));
		painter.setFont(valueFont);
		painter.drawText(rect, Qt::AlignCenter, state.valueText);
	}
}

// The two style defaults are the strings FilterCardRow::refreshStateProperties
// computed before the hook existed, moved verbatim, so every skin keeps its
// previous card chrome until it overrides them.
QString ISkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
	const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
	// Signal Matrix uses a coloured rail on the left edge to suggest routing.
	// All other skins keep a uniform 1px border.
	return tokens.cardRailWidth > 0
		? QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-left: %3px solid %4; border-radius: %5px; }")
		.arg(backgroundColor, borderColor)
		.arg(tokens.cardRailWidth)
		.arg(tokens.accent)
		.arg(tokens.borderRadius)
		: QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }")
		.arg(backgroundColor, borderColor)
		.arg(tokens.borderRadius);
}

QString ISkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	return QStringLiteral("QWidget#FilterCardHeader { background: %1; border-top-left-radius: %2px; border-top-right-radius: %2px; }")
		.arg(info.selected ? tokens.surfaceRaised : tokens.cardHover)
		.arg(tokens.borderRadius);
}

// The default is the string FilterCardRow::rebuildSummary computed before the
// hook existed, moved verbatim: outline-style skins ink the badge in the type
// colour, filled-style skins use the type colour as the pill background.
QString ISkin::typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& tokens) const
{
	Q_UNUSED(info);
	const bool outlineBadge = tokens.badgeStyle == SkinTokens::OutlineOnly || tokens.badgeStyle == SkinTokens::WireframeBorder;
	return QStringLiteral("color:%1; border-color:%2; background-color:%3;")
		.arg(outlineBadge ? typeColor : QStringLiteral("white"),
			typeColor,
			outlineBadge ? QStringLiteral("transparent") : typeColor);
}

void ISkin::prepareCommandRow(const CommandRowInfo&, QWidget*, QWidget*, QWidget*) const
{
	// Neutral default: rows keep their stock construction.
}

void ISkin::paintCardChrome(QPainter&, const QRect&, const CommandRowInfo&, const SkinTokens&) const
{
	// Neutral default: no painted decoration on top of the QSS chrome.
}

FilterPickerView* ISkin::createFilterPicker(QWidget* parent) const
{
	// Neutral default: the shared search-over-sections dropdown.
	return new DefaultFilterPickerView(parent);
}

void ISkin::paintTitleBarChrome(QPainter&, const QRect&, const SkinTokens&) const
{
	// Neutral default: the QSS background is the whole story.
}

void ISkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;

	// Neutral default: the shared modern stroke icons, tinted with the text
	// token so they follow every skin's dark/light ink. This replaces the
	// legacy .ico set the .ui file still references (kept there so a skin
	// could deliberately return to it).
	const QColor ink(tokens.text);
	toolBar->setIconSize(GUIHelper::scale(QSize(18, 18)));
	for (QAction* action : toolBar->actions())
	{
		if (action->objectName() == QStringLiteral("actionNew"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/file-new.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionOpen"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionSave"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save.svg"), ink, 18));
	}
}
