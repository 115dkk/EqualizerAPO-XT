/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QEvent>
#include <QPainter>
#include <QToolBar>
#include <QWidget>

#include "Editor/skins/shared/SkinChromeOverlay.h"
#include "MatrixSkinDetail.h"

namespace
{

// Painted chrome layers for the main toolbar.
// QSS cannot draw the 24px column grid or the status lamp, so the matrix
// toolbar hook parents two transparent, mouse-transparent widgets to the
// toolbar: UnderCells (lowered below every cell) paints the column grid,
// the doubled header rule and the sunken fill of the status readout cell;
// OverCells (raised above the cells) paints the DirtyStatusBadge lamp on
// top of that readout. Instances are found again by object name on every
// hook run (this file has no moc, so findChild by class is unavailable),
// and painting self-suspends while another skin is active because the real
// MainWindow toolbar keeps its children across runtime skin switches.
class MatrixToolbarBoard : public SkinChromeOverlay
{
public:
	enum Layer { UnderCells, OverCells };

	MatrixToolbarBoard(QToolBar* toolBar, Layer boardLayer)
		: SkinChromeOverlay(toolBar,
			boardLayer == UnderCells
				? QStringLiteral("MatrixToolbarBoardUnder")
				: QStringLiteral("MatrixToolbarBoardOver"),
			QStringLiteral("matrix"),
			boardLayer == UnderCells ? ZPolicy::BelowControls : ZPolicy::AboveControls),
		layer(boardLayer)
	{
		if (layer == OverCells)
		{
			// The lamp must follow the badge's dirty-state restyles and the
			// layout moving the cell around.
			if (QWidget* badge = toolBar->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge")))
				badge->installEventFilter(this);
		}
		refreshOverlay();
	}

	void setBoardTokens(const SkinTokens& tokens)
	{
		ruleColor = QColor(tokens.border);
		sunkenColor = QColor(tokens.surfaceSunken);
		savedColor = QColor(tokens.success);
		modifiedColor = QColor(tokens.warning);
		// The hook carries no mode flag; infer it from the strip's surface
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		gridAlpha = tokens.dark ? 55 : 90;
		update();
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched != parentToolBar()
			&& (event->type() == QEvent::Paint || event->type() == QEvent::Move
			|| event->type() == QEvent::Resize || event->type() == QEvent::Show
			|| event->type() == QEvent::Hide))
			update();
		return SkinChromeOverlay::eventFilter(watched, event);
	}

protected:
	void paintChrome(QPainter& painter) override
	{
		painter.setRenderHint(QPainter::Antialiasing, false);
		if (layer == UnderCells)
			paintBoard(painter);
		else
			paintLamp(painter);
	}

private:
	// The badge is only board chrome while the skin owns its appearance.
	// MainWindow::updateDirtyStatus replaces it with an inline-styled pill
	// at runtime; painting a lamp under that pill would garble its text.
	QWidget* ownedBadge() const
	{
		QWidget* badge = parentToolBar()->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge"));
		if (badge == nullptr || !badge->isVisible() || !badge->styleSheet().isEmpty())
			return nullptr;
		return badge;
	}

	void paintBoard(QPainter& painter)
	{
		// Faint 24px column grid, same pitch and ink as the card grid texture.
		QColor grid(ruleColor);
		grid.setAlpha(gridAlpha);
		painter.setPen(QPen(grid, 1));
		for (int x = MatrixMetrics::gridPitch; x < width(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, 0, x, height());

		// Doubled header rule: this inner line plus the QSS bottom border.
		painter.setPen(QPen(ruleColor, 1));
		painter.drawLine(0, height() - 4, width(), height() - 4);

		// Sunken fill behind the status readout cell (the badge's own QSS
		// background stays transparent so this fill and the lamp show).
		if (QWidget* badge = ownedBadge())
			painter.fillRect(badge->geometry().adjusted(1, 1, -1, -1), sunkenColor);
	}

	void paintLamp(QPainter& painter)
	{
		QWidget* badge = ownedBadge();
		if (badge == nullptr)
			return;
		// Solid square lamp: green = saved, amber = modified.
		const QRect cell = badge->geometry();
		const QRect lampRect(cell.left() + 8, cell.center().y() - 4, 8, 8);
		painter.fillRect(lampRect, badge->property("dirty").toBool() ? modifiedColor : savedColor);
	}

	Layer layer;
	QColor ruleColor;
	QColor sunkenColor;
	QColor savedColor;
	QColor modifiedColor;
	int gridAlpha = 55;
};

}

void MatrixSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
		if (toolBar == nullptr)
			return;

		// Shared modern stroke icons, tinted with the text token.
		ISkin::styleMainToolbar(toolBar, tokens);
		toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

		// Painted board layers: created once per toolbar, re-tinted on every
		// call (dark/light switches reuse the same instances).
		auto boardLayer = [toolBar](const QString& name, MatrixToolbarBoard::Layer layer)
		{
			QWidget* existing = toolBar->findChild<QWidget*>(name, Qt::FindDirectChildrenOnly);
			MatrixToolbarBoard* board = existing != nullptr
				? static_cast<MatrixToolbarBoard*>(existing)
				: new MatrixToolbarBoard(toolBar, layer);
			board->refreshOverlay();
			return board;
		};
		boardLayer(QStringLiteral("MatrixToolbarBoardUnder"), MatrixToolbarBoard::UnderCells)->setBoardTokens(tokens);
		boardLayer(QStringLiteral("MatrixToolbarBoardOver"), MatrixToolbarBoard::OverCells)->setBoardTokens(tokens);
	}
