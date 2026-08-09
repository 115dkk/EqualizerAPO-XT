/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFileDialog>
#include <QIcon>
#include <QPainter>
#include <QPen>

#include "Editor/skins/shared/SkinFileIcons.h"

namespace
{
// File-dialog pictograms in Minimal's language: square-cornered hairline
// outlines in body ink, drawn without antialiasing so every line lands on
// the pixel grid like the rest of the terminal. The console feel comes from
// the geometry (hard edges, hairlines, a filled cursor block as the only
// solid), NOT from ASCII art - the user wants "looks like a console", not a
// literal one (round-2 verdict). Icons must stay clearly visible.
class MinimalFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			// Integer grid: everything derives from s and lands on whole
			// pixels; hairlines stay 1px up to 32, 2px above.
			painter.setRenderHint(QPainter::Antialiasing, false);
			const int s = sizePx;
			const int line = s >= 40 ? 2 : 1;
			painter.setPen(QPen(ink, line));
			painter.setBrush(Qt::NoBrush);
			const auto px = [s](double f) { return int(f * s + 0.5); };

			const auto docOutline = [&]() {
				// Sharp-cornered sheet with a squared step notch instead of a
				// diagonal fold: terminals do not do diagonals.
				const QPoint points[] = {
					{ px(0.25), px(0.12) }, { px(0.61), px(0.12) }, { px(0.61), px(0.26) },
					{ px(0.75), px(0.26) }, { px(0.75), px(0.88) }, { px(0.25), px(0.88) }
				};
				painter.drawPolygon(points, 6);
			};

			switch (glyph)
			{
			case Glyph::Folder:
				// Tab + body, and the terminal's one solid: a cursor block
				// parked in the tab.
				painter.drawRect(px(0.12), px(0.26), px(0.30), px(0.10));
				painter.drawRect(px(0.12), px(0.36), px(0.76), px(0.42));
				painter.fillRect(px(0.17), px(0.29), px(0.08), px(0.05), ink);
				break;
			case Glyph::ConfigFile:
				docOutline();
				painter.drawLine(px(0.33), px(0.44), px(0.67), px(0.44));
				painter.drawLine(px(0.33), px(0.56), px(0.67), px(0.56));
				painter.drawLine(px(0.33), px(0.68), px(0.55), px(0.68));
				break;
			case Glyph::AudioFile:
				docOutline();
				// A tiny level meter: three bars, the terminal's idea of audio.
				painter.fillRect(px(0.34), px(0.58), px(0.08), px(0.14), ink);
				painter.fillRect(px(0.46), px(0.48), px(0.08), px(0.24), ink);
				painter.fillRect(px(0.58), px(0.64), px(0.08), px(0.08), ink);
				break;
			case Glyph::PluginFile:
				docOutline();
				painter.drawRect(px(0.38), px(0.48), px(0.24), px(0.20));
				painter.drawLine(px(0.44), px(0.48), px(0.44), px(0.40));
				painter.drawLine(px(0.56), px(0.48), px(0.56), px(0.40));
				break;
			case Glyph::GenericFile:
				docOutline();
				break;
			case Glyph::Drive:
				painter.drawRect(px(0.12), px(0.32), px(0.76), px(0.36));
				painter.drawLine(px(0.20), px(0.56), px(0.50), px(0.56));
				painter.fillRect(px(0.72), px(0.52), px(0.08), px(0.08), ink);
				break;
			case Glyph::Computer:
				painter.drawRect(px(0.14), px(0.18), px(0.72), px(0.44));
				painter.fillRect(px(0.22), px(0.26), px(0.08), px(0.05), ink);
				painter.drawLine(px(0.50), px(0.62), px(0.50), px(0.74));
				painter.drawLine(px(0.34), px(0.78), px(0.66), px(0.78));
				break;
			}
		});
	}
};
}

void MinimalSkin::installFileIconProvider(QFileDialog* dialog, const SkinTokens& tokens)
{
	static MinimalFileIconProvider iconProvider;
	iconProvider.updateTokens(tokens);
	dialog->setIconProvider(&iconProvider);
}
