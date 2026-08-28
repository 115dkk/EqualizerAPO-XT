/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QAction>
#include <QCheckBox>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QToolBar>

#include "Editor/skins/shared/SkinChromeOverlay.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "RackSkinDetail.h"

namespace
{

const char* const kToolbarPlateName = "RackToolbarPlate";
const char* const kToolbarEarSpacerName = "RackToolbarEarSpacer";
// Width of the rail-ear zone at both toolbar ends. QToolBar ignores
// stylesheet padding and QToolBarLayout cannot carry asymmetric margins, so
// the zones are reserved by two fixed-width spacer widgets at the ends of
// the action train; the painter reads their live geometry back, so an ear
// that no longer fits (toolbar overflow) simply is not painted.
const int kRailEarWidth = 24;

// Painted master-rail decoration: everything the QSS base rail cannot
// express. Drawn by the RackToolbarPlate overlay between the toolbar's
// stylesheet background and its controls, in the same faceplate grammar as
// the card chrome so the rail reads as the rack's master section.
void paintToolbarRail(QPainter& painter, const QRect& rect, const QToolBar* toolBar, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect);

	// Brushed-metal sheen: the rolled top edge falling into shadow.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
		sheen.setColorAt(0.14, QColor(255, 255, 255, 10));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 52));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 120));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 30));
	}
	painter.fillRect(r, sheen);

	// Horizontal brushing grain, same machine as the card faceplates.
	RackSkinDetail::paintBrushing(painter, r, dark, uint(qHash(QStringLiteral("master-rail-brush"))));

	// Machined edges: lit top chamfer, shadowed groove above the QSS border,
	// so the strip reads as a milled rail rather than a flat band.
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
	painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

	// Rail ears with one mounting screw each, painted at the live geometry of
	// the ear spacers (an ear pushed out by toolbar overflow is not painted,
	// so the screw never sits under a control). The slot angles differ -
	// hand-tightened, like the card corners.
	const uint seed = uint(qHash(QStringLiteral("master-rail")));
	const QColor earFill(0, 0, 0, dark ? 52 : 20);
	for (const QWidget* spacer : toolBar->findChildren<QWidget*>(QLatin1String(kToolbarEarSpacerName), Qt::FindDirectChildrenOnly))
	{
		if (!spacer->isVisible() || spacer->width() < kRailEarWidth - 4)
			continue;
		const QRectF g(spacer->geometry());
		const bool leftSide = g.center().x() < r.center().x();
		// The ear runs from the rail edge to the machined groove that
		// separates it from the panel.
		const QRectF ear = leftSide
			? QRectF(r.left(), r.top(), g.right() - r.left(), r.height())
			: QRectF(g.left(), r.top(), r.right() - g.left(), r.height());
		const qreal grooveX = leftSide ? ear.right() : ear.left();
		painter.fillRect(ear, earFill);
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
		painter.drawLine(QPointF(grooveX, r.top()), QPointF(grooveX, r.bottom()));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
		painter.drawLine(QPointF(grooveX + (leftSide ? 1 : -1), r.top()), QPointF(grooveX + (leftSide ? 1 : -1), r.bottom()));
		RackSkinDetail::paintScrew(painter, QPointF(ear.center().x(), r.center().y()), 4.0,
			qreal((seed + (leftSide ? 0u : 73u)) % 180u), dark);
	}

	// Engraved section designation on the blank panel between the save-state
	// readout and the device selectors (the expanding spacer), drawn only
	// when the blank leaves room. Hardware printing, not a UI string -
	// never translated.
	const QString marking = QStringLiteral("MASTER");
	QFont markFont(tokens.fontFamily);
	markFont.setPixelSize(8);
	markFont.setBold(true);
	markFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
	const QFontMetricsF metrics(markFont);
	const QWidget* blank = nullptr;
	for (const QWidget* spacer : toolBar->findChildren<QWidget*>(QStringLiteral("ToolBarSpacer"), Qt::FindDirectChildrenOnly))
		if (blank == nullptr || spacer->width() > blank->width())
			blank = spacer;
	if (blank != nullptr && blank->width() >= metrics.horizontalAdvance(marking) + 16)
	{
		painter.setFont(markFont);
		QColor ink(tokens.mutedText);
		ink.setAlpha(dark ? 150 : 190);
		RackSkinDetail::engraveText(painter, QRectF(blank->geometry()), Qt::AlignCenter, marking, ink, dark);
	}

	// The instant-mode power LED, mounted in the well the checkbox's QSS
	// padding reserves (its indicator is collapsed): lit green while the
	// mode is engaged, a dark dome when it is off.
	if (const QCheckBox* box = toolBar->findChild<QCheckBox*>(QStringLiteral("InstantModeCheckBox"), Qt::FindDirectChildrenOnly))
	{
		const QRectF g(box->geometry());
		RackSkinDetail::paintLed(painter, QPointF(g.left() + 10.0, g.center().y()), 3.2, QColor(tokens.accent2),
			box->isChecked() && box->isEnabled(), dark);
	}
}

// The transparent overlay carrying the painted rail chrome. It sits as the
// bottom-most child of the toolbar (above the QSS background, below the
// controls), tracks the toolbar's size, repaints when the instant-mode
// switch toggles and hides itself when another skin's stylesheet takes over
// (every skin switch delivers StyleChange to the toolbar). No Q_OBJECT: it
// is found again by object name and only connects to existing signals.
class RackToolbarPlate : public SkinChromeOverlay
{
public:
	explicit RackToolbarPlate(QToolBar* parentToolBar)
		: SkinChromeOverlay(parentToolBar, QLatin1String(kToolbarPlateName),
			QStringLiteral("rack"), ZPolicy::BelowControls)
	{
		if (QCheckBox* box = parentToolBar->findChild<QCheckBox*>(QStringLiteral("InstantModeCheckBox"), Qt::FindDirectChildrenOnly))
			connect(box, &QCheckBox::toggled, this, QOverload<>::of(&QWidget::update));
	}

	void setTokens(const SkinTokens& newTokens)
	{
		tokens = newTokens;
	}

	// The actions wrapping the two ear spacers; their visibility follows the
	// plate's, so the reserved zones vanish with the chrome when another
	// skin takes over.
	void setEarActions(QAction* left, QAction* right)
	{
		leftEarAction = left;
		rightEarAction = right;
	}

	// The StyleChange filter below hides the ears when another skin's sheet
	// arrives, but a same-skin re-apply is skipped upstream and delivers no
	// StyleChange - the reuse path calls this so the zones come back with
	// the chrome regardless of how the hook was reached.
	void showEarZones()
	{
		if (leftEarAction != nullptr)
			leftEarAction->setVisible(true);
		if (rightEarAction != nullptr)
			rightEarAction->setVisible(true);
	}

protected:
	void ownerActiveChanged(bool active) override
	{
		if (leftEarAction != nullptr)
			leftEarAction->setVisible(active);
		if (rightEarAction != nullptr)
			rightEarAction->setVisible(active);
	}

	void paintChrome(QPainter& painter) override
	{
		paintToolbarRail(painter, rect(), parentToolBar(), tokens);
	}

private:
	QAction* leftEarAction = nullptr;
	QAction* rightEarAction = nullptr;
	SkinTokens tokens;
};

// A fixed-width blank widget reserving one rail-ear zone in the toolbar's
// action train. The plate paints the ear and its screw at this widget's
// live geometry.
QWidget* makeEarSpacer(QWidget* parent)
{
	QWidget* spacer = new QWidget(parent);
	spacer->setObjectName(QLatin1String(kToolbarEarSpacerName));
	spacer->setFixedWidth(kRailEarWidth);
	spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
	// Reserved space only: without this the universal QWidget background
	// rule stamps an opaque patch over the rail ear the plate paints.
	spacer->setAttribute(Qt::WA_NoSystemBackground, true);
	return spacer;
}

}

void RackSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	// Preserve the shared action icons before mounting Rack's rail overlay.
	ISkin::styleMainToolbar(toolBar, tokens);
	if (toolBar == nullptr)
		return;

	// Hook runs at startup and on every skin/dark switch: reuse the plate if
	// it is already mounted. Only this file ever creates a child with that
	// object name, so the static_cast is safe (the class has no Q_OBJECT, so
	// findChild on the concrete type would match any QWidget).
	QWidget* existing = toolBar->findChild<QWidget*>(QLatin1String(kToolbarPlateName), Qt::FindDirectChildrenOnly);
	RackToolbarPlate* plate;
	if (existing != nullptr)
	{
		plate = static_cast<RackToolbarPlate*>(existing);
	}
	else
	{
		plate = new RackToolbarPlate(toolBar);
		// Reserve the rail-ear zones once, at both ends of the action train.
		// The plate toggles these actions with its own visibility, so the
		// zones leave with the chrome on a skin switch and return with it.
		const QList<QAction*> actions = toolBar->actions();
		QAction* leftEar = actions.isEmpty()
			? toolBar->addWidget(makeEarSpacer(toolBar))
			: toolBar->insertWidget(actions.first(), makeEarSpacer(toolBar));
		QAction* rightEar = toolBar->addWidget(makeEarSpacer(toolBar));
		plate->setEarActions(leftEar, rightEar);
	}
	plate->setTokens(tokens);
	plate->showEarZones();
	plate->refreshOverlay();
}
