/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "Editor/skins/shared/SkinPaint.h"
#include "MatrixSkinDetail.h"

void MatrixSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
		painter.setRenderHint(QPainter::Antialiasing, false);

		const QRect cell = rect.adjusted(0, 0, -1, -1);
		if (cell.width() <= 0 || cell.height() <= 0)
			return;

		// The board surface's graph paper: the same faint 24px column grid
		// the masthead and toolbar sit on.
		QColor grid(tokens.border);
		grid.setAlpha(tokens.dark ? 55 : 90);
		painter.setPen(QPen(grid, 1));
		for (int x = cell.left() + MatrixMetrics::gridPitch; x < cell.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, cell.top() + 1, x, cell.bottom() - 1);

		// Crosspoint pre-light: row band + coordinate-column band; their
		// overlap in the coordinate band is the crosspoint.
		if (state.hovered || state.pressed)
		{
			QColor rowBand(tokens.accent);
			rowBand.setAlpha(state.pressed ? 28 : 22);
			painter.fillRect(cell, rowBand);
			QColor columnColor(tokens.accent);
			columnColor.setAlpha(14);
			painter.fillRect(QRect(cell.left(), cell.top(),
				qMin(MatrixMetrics::coordinateBandWidth, cell.width()), cell.height()), columnColor);
		}

		// Outer rule: dashed while the slot is vacant, solid accent while
		// being engaged (pressed = the picker is about to post here). Hover
		// pre-lights the dash in accent; keyboard focus is NOT a pre-light -
		// it gets the square cell bracket below (the knob focus grammar), so
		// a merely focused slot still reads at rest.
		const bool preLit = state.hovered;
		painter.setPen(QPen(QColor(state.pressed || preLit ? tokens.accent : tokens.border), 1,
			state.pressed ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(cell);

		// Designation cell: muted at rest, accent pre-light on hover, LED
		// fill while engaged.
		QFont mono(tokens.monoFontFamily);
		mono.setPointSizeF(9.0);
		mono.setBold(true);
		const QFontMetrics monoMetrics(mono);
		const int designationWidth = monoMetrics.horizontalAdvance(QStringLiteral("+")) + 12;
		const QRect designationRect(cell.left() + 10, cell.center().y() - 9, designationWidth, 18);
		painter.setPen(QPen(QColor(state.pressed || preLit ? tokens.accent : tokens.border), 1));
		painter.setBrush(state.pressed ? QColor(tokens.accent) : QColor(tokens.surfaceSunken));
		painter.drawRect(designationRect.adjusted(0, 0, -1, -1));
		painter.setFont(mono);
		painter.setPen(state.pressed ? QColor(tokens.background)
			: (preLit ? QColor(tokens.accent) : QColor(tokens.mutedText)));
		painter.drawText(designationRect, Qt::AlignCenter, QStringLiteral("+"));

		// Keyboard focus: a square cell bracket around the designation cell.
		if (state.focused && !state.pressed)
		{
			painter.setPen(QPen(QColor(tokens.accent), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(designationRect.adjusted(-3, -3, 2, 2));
		}

		// Mono board caption; body ink while the crosspoint is lit.
		QFont caption(tokens.monoFontFamily);
		caption.setPointSizeF(8.0);
		caption.setWeight(QFont::DemiBold);
		caption.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(caption);
		painter.setPen(QColor(state.hovered || state.pressed ? tokens.text : tokens.mutedText));
		painter.drawText(QRect(designationRect.right() + 11, cell.top(), qMax(0, cell.right() - designationRect.right() - 12), cell.height()),
			Qt::AlignVCenter | Qt::AlignLeft, state.label.toUpper());
	}

void MatrixSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
		if (!state.hovered && !state.pressed)
			return;

		painter.setRenderHint(QPainter::Antialiasing, false);
		const QColor accent(tokens.accent);
		const int centerY = rect.center().y();
		const int side = qMin(rect.height(), 14);
		if (side <= 2 || rect.width() <= side)
			return;

		// Square insertion cell: sunken well + accent rule while pre-lit,
		// LED fill while pressed.
		const QRect cellRect(rect.left(), centerY - side / 2, side, side);
		painter.setPen(QPen(accent, 1));
		painter.setBrush(state.pressed ? accent : QColor(tokens.surfaceSunken));
		painter.drawRect(cellRect.adjusted(0, 0, -1, -1));

		QFont mono(tokens.monoFontFamily);
		mono.setPixelSize(qMax(6, side - 3));
		mono.setBold(true);
		painter.setFont(mono);
		painter.setPen(state.pressed ? QColor(tokens.background) : accent);
		painter.drawText(cellRect.adjusted(0, 0, -1, -1), Qt::AlignCenter, QStringLiteral("+"));

		// The 1px insertion rule across the boundary.
		painter.setPen(QPen(accent, 1));
		painter.drawLine(cellRect.right() + 5, centerY, rect.right() - 1, centerY);
	}
