/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VSTBusStrip.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Every layout the config grammar accepts, in menu order (VST3BusLayout.h is
// the authority on names and widths).
const VST3BusLayout kMenuLayouts[] = {
	VST3BusLayout::Auto, VST3BusLayout::Mono, VST3BusLayout::Stereo,
	VST3BusLayout::Surround40, VST3BusLayout::Surround41, VST3BusLayout::Surround50,
	VST3BusLayout::Surround51, VST3BusLayout::Surround61, VST3BusLayout::Surround71,
	VST3BusLayout::Surround712, VST3BusLayout::Surround714
};

QString layoutName(VST3BusLayout layout)
{
	return QString::fromWCharArray(vst3BusLayoutName(layout));
}

int selectorHeight()
{
	return GUIHelper::scale(20.0);
}

// The selector's width is computed from the widest layout token so a pick
// never re-flows the row (and the offscreen galleries stay deterministic).
int selectorWidth(bool output)
{
	const SkinTokens& t = SkinManager::instance()->tokens();
	QFont valueFont(t.monoFontFamily);
	valueFont.setPixelSize(GUIHelper::scale(12.0));
	const QFontMetricsF valueMetrics(valueFont);
	qreal widestValue = 0;
	for (VST3BusLayout layout : kMenuLayouts)
	{
		const int channels = vst3BusLayoutChannelCount(layout);
		QString value = layoutName(layout);
		if (channels > 0)
			value += QStringLiteral(" %1").arg(channels);
		widestValue = qMax(widestValue, valueMetrics.horizontalAdvance(value));
	}

	QFont roleFont(t.fontFamily);
	roleFont.setPixelSize(GUIHelper::scale(9.0));
	const QFontMetricsF roleMetrics(roleFont);
	const QString roleToken = output ? QStringLiteral("OUT") : QStringLiteral("IN");
	const QString roleText = output
		? VSTBusStrip::tr("Out") : VSTBusStrip::tr("In");
	const qreal roleWidth = qMax(roleMetrics.horizontalAdvance(roleToken),
		roleMetrics.horizontalAdvance(roleText));

	// Lean paddings on purpose: the strip shares one wide row with the
	// identity and the action buttons, and every reserved pixel here is a
	// pixel the location and name columns lose at 960px.
	return qRound(GUIHelper::scale(6.0) * 2	// cell padding
		+ roleWidth + GUIHelper::scale(4.0)
		+ widestValue + GUIHelper::scale(3.0)
		+ GUIHelper::scale(6.0));	// caret
}
}

// ---- VSTBusSelector --------------------------------------------------------

VSTBusSelector::VSTBusSelector(bool output, QWidget* parent)
	: QWidget(parent), output(output)
{
	setObjectName(output
		? QStringLiteral("VSTBusSelectorOut") : QStringLiteral("VSTBusSelectorIn"));
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
	setAccessibleName(output
		? VSTBusStrip::tr("VST3 output bus layout")
		: VSTBusStrip::tr("VST3 input bus layout"));
	refreshAccessibleValue();
}

VST3BusLayout VSTBusSelector::busLayout() const
{
	return current;
}

void VSTBusSelector::setBusLayout(VST3BusLayout layout)
{
	if (current == layout)
		return;
	current = layout;
	refreshAccessibleValue();
	update();
}

QSize VSTBusSelector::sizeHint() const
{
	return QSize(selectorWidth(output), selectorHeight());
}

QSize VSTBusSelector::minimumSizeHint() const
{
	return sizeHint();
}

void VSTBusSelector::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	VstBusSelectorState state;
	state.rect = rect();
	state.output = output;
	state.roleToken = output ? QStringLiteral("OUT") : QStringLiteral("IN");
	state.roleText = output ? VSTBusStrip::tr("Out") : VSTBusStrip::tr("In");
	state.layoutText = layoutName(current);
	state.channelCount = vst3BusLayoutChannelCount(current);
	state.enabled = isEnabled();
	state.hovered = hovered;
	state.pressed = pressed;
	state.focused = hasFocus();
	state.menuOpen = menuOpen;
	SkinManager::instance()->paintVstBusSelector(painter, state);
}

void VSTBusSelector::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}
	pressed = true;
	update();
}

void VSTBusSelector::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const bool inside = pressed && rect().contains(event->pos());
	pressed = false;
	update();
	if (inside)
		openMenu();
}

void VSTBusSelector::keyPressEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Space:
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Down:
		openMenu();
		return;
	default:
		QWidget::keyPressEvent(event);
	}
}

void VSTBusSelector::enterEvent(QEnterEvent*)
{
	hovered = true;
	update();
}

void VSTBusSelector::leaveEvent(QEvent*)
{
	hovered = false;
	update();
}

void VSTBusSelector::focusInEvent(QFocusEvent*)
{
	update();
}

void VSTBusSelector::focusOutEvent(QFocusEvent*)
{
	update();
}

void VSTBusSelector::openMenu()
{
	QMenu menu(this);
	for (VST3BusLayout layout : kMenuLayouts)
	{
		const int channels = vst3BusLayoutChannelCount(layout);
		// The tab column right-aligns the widths the way shortcut hints sit
		// in every other menu.
		const QString label = channels > 0
			? QStringLiteral("%1\t%2 ch").arg(layoutName(layout)).arg(channels)
			: layoutName(layout);
		QAction* action = menu.addAction(label);
		action->setCheckable(true);
		action->setChecked(layout == current);
		action->setData(static_cast<int>(layout));
	}

	menuOpen = true;
	update();
	QAction* chosen = menu.exec(mapToGlobal(QPoint(0, height())));
	menuOpen = false;
	update();

	if (chosen == nullptr)
		return;
	const VST3BusLayout layout = static_cast<VST3BusLayout>(chosen->data().toInt());
	setBusLayout(layout);
	emit busLayoutPicked(layout);
}

void VSTBusSelector::refreshAccessibleValue()
{
	setAccessibleDescription(layoutName(current));
}

// ---- VSTBusStrip -----------------------------------------------------------

namespace
{
int hMargin()
{
	return GUIHelper::scale(4.0);
}

int vMargin()
{
	return GUIHelper::scale(3.0);
}

int jointWidth()
{
	return GUIHelper::scale(14.0);
}

int verdictGap()
{
	return GUIHelper::scale(6.0);
}
}

VSTBusStrip::VSTBusStrip(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("VSTBusStrip"));

	inputSelector = new VSTBusSelector(false, this);
	outputSelector = new VSTBusSelector(true, this);
	setTabOrder(inputSelector, outputSelector);

	const auto forward = [this](VST3BusLayout) {
		emit busLayoutsPicked(inputSelector->busLayout(), outputSelector->busLayout());
	};
	connect(inputSelector, &VSTBusSelector::busLayoutPicked, this, forward);
	connect(outputSelector, &VSTBusSelector::busLayoutPicked, this, forward);
}

VST3BusLayout VSTBusStrip::inputLayout() const
{
	return inputSelector->busLayout();
}

VST3BusLayout VSTBusStrip::outputLayout() const
{
	return outputSelector->busLayout();
}

void VSTBusStrip::setBusLayouts(VST3BusLayout input, VST3BusLayout output)
{
	inputSelector->setBusLayout(input);
	outputSelector->setBusLayout(output);
}

void VSTBusStrip::setSelectorsEnabled(bool enabled, const QString& disabledReason)
{
	const QString tip = enabled ? QString() : disabledReason;
	for (VSTBusSelector* selector : {inputSelector, outputSelector})
	{
		selector->setEnabled(enabled);
		selector->setToolTip(tip);
	}
	update();
}

void VSTBusStrip::setVerdict(const QString& text, VstBusFrameState::Tone tone)
{
	if (verdictText == text && verdictInputText.isEmpty() && verdictOutputText.isEmpty()
		&& verdictTone == tone)
		return;
	verdictText = text;
	verdictInputText.clear();
	verdictOutputText.clear();
	verdictTone = tone;
	updateGeometry();
	relayout();
	update();
}

void VSTBusStrip::setVerdictPair(const QString& input, const QString& output, VstBusFrameState::Tone tone)
{
	if (verdictText.isEmpty() && verdictInputText == input && verdictOutputText == output
		&& verdictTone == tone)
		return;
	verdictText.clear();
	verdictInputText = input;
	verdictOutputText = output;
	verdictTone = tone;
	updateGeometry();
	relayout();
	update();
}

QSize VSTBusStrip::sizeHint() const
{
	int width = hMargin() * 2
		+ inputSelector->sizeHint().width()
		+ jointWidth()
		+ outputSelector->sizeHint().width();
	const int verdict = verdictWidth();
	if (verdict > 0)
		width += verdictGap() + verdict;
	return QSize(width, selectorHeight() + vMargin() * 2);
}

QSize VSTBusStrip::minimumSizeHint() const
{
	return sizeHint();
}

void VSTBusStrip::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	SkinManager::instance()->paintVstBusFrame(painter, frameState());
}

void VSTBusStrip::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	relayout();
}

VstBusFrameState VSTBusStrip::frameState() const
{
	VstBusFrameState state;
	state.rect = rect();
	state.inputRect = inputSelector->geometry();
	state.outputRect = outputSelector->geometry();
	state.jointRect = QRect(state.inputRect.right() + 1, state.inputRect.top(),
		state.outputRect.left() - state.inputRect.right() - 1, state.inputRect.height());
	state.verdictRect = QRect(state.outputRect.right() + 1 + verdictGap(), state.outputRect.top(),
		qMax(0, width() - hMargin() - (state.outputRect.right() + 1 + verdictGap())),
		state.outputRect.height());
	state.verdictText = verdictText;
	state.verdictInputText = verdictInputText;
	state.verdictOutputText = verdictOutputText;
	state.tone = verdictTone;
	state.enabled = inputSelector->isEnabled();
	return state;
}

void VSTBusStrip::relayout()
{
	const int y = (height() - selectorHeight()) / 2;
	int x = hMargin();
	inputSelector->setGeometry(x, y, inputSelector->sizeHint().width(), selectorHeight());
	x += inputSelector->width() + jointWidth();
	outputSelector->setGeometry(x, y, outputSelector->sizeHint().width(), selectorHeight());
}

int VSTBusStrip::verdictWidth() const
{
	// Nothing to read: no area. A tone without words (an accepted explicit
	// contract, where the selectors already print the pair) is a lamp-only
	// readout - the card is wide, but not wide enough to say things twice.
	const bool hasText = !verdictText.isEmpty()
		|| !verdictInputText.isEmpty() || !verdictOutputText.isEmpty();
	if (!hasText)
		return verdictTone == VstBusFrameState::Tone::Neutral ? 0 : GUIHelper::scale(14.0);

	const SkinTokens& t = SkinManager::instance()->tokens();
	QFont verdictFont(t.monoFontFamily);
	verdictFont.setPixelSize(GUIHelper::scale(10.0));
	const QFontMetricsF metrics(verdictFont);
	// Lamp allowance + text (a pair adds the painted direction mark), capped
	// so a translated verdict cannot push the action buttons off the row;
	// the paint side elides.
	int text;
	if (!verdictInputText.isEmpty() || !verdictOutputText.isEmpty())
		text = qRound(metrics.horizontalAdvance(verdictInputText)
			+ metrics.horizontalAdvance(verdictOutputText)) + GUIHelper::scale(16.0);
	else
		// The word register: a small slack on top of the measure, because
		// letter-spaced board/engraved verdicts render wider than the plain
		// metric this reserve is computed with.
		text = qRound(metrics.horizontalAdvance(verdictText)) + GUIHelper::scale(5.0);
	return qMin(GUIHelper::scale(150.0), GUIHelper::scale(12.0) + text + GUIHelper::scale(6.0));
}
