/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Neutral base forms for the Device Selector chrome plus the id -> painter
	factory. The base is deliberately plain (readable rows, a quiet toggle,
	flat buttons); each skin subclass replaces the forms wholesale with its
	own instrument. Painting rules: antialiased, half-pixel aligned strokes,
	no hardcoded colours - every colour derives from the token struct.
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPainterPath>

#include "Editor/skins/SkinPaint.h"

const DeviceSkinPainter* studioDeviceSkinPainter();
const DeviceSkinPainter* minimalDeviceSkinPainter();
const DeviceSkinPainter* softDeviceSkinPainter();
const DeviceSkinPainter* rackDeviceSkinPainter();
const DeviceSkinPainter* matrixDeviceSkinPainter();

const DeviceSkinPainter* DeviceSkinPainter::forSkin(const QString& skinId)
{
	const QString id = SkinThemeData::resolveId(skinId);
	if (id == QStringLiteral("minimal"))
		return minimalDeviceSkinPainter();
	if (id == QStringLiteral("soft"))
		return softDeviceSkinPainter();
	if (id == QStringLiteral("rack"))
		return rackDeviceSkinPainter();
	if (id == QStringLiteral("matrix"))
		return matrixDeviceSkinPainter();
	return studioDeviceSkinPainter();
}

namespace
{
struct ActiveTheme
{
	const DeviceSkinPainter* painter = nullptr;
	SkinTokens tokens;
};

ActiveTheme& activeTheme()
{
	static ActiveTheme theme{ DeviceSkinPainter::forSkin(QStringLiteral("studio")),
		SkinThemeData::tokens(QStringLiteral("studio"), true) };
	return theme;
}
}

void DeviceSkinPainter::setActiveTheme(const QString& skinId, bool dark)
{
	activeTheme().painter = forSkin(skinId);
	activeTheme().tokens = SkinThemeData::tokens(skinId, dark);
}

void DeviceSkinPainter::setHeritageTheme()
{
	static const DeviceSkinPainter neutral;
	SkinTokens tokens; // classic light values, mirroring SkinManager::applyHeritage
	tokens.background = QStringLiteral("#f0f0f0");
	tokens.surface = QStringLiteral("#ffffff");
	tokens.surfaceRaised = QStringLiteral("#f5f5f5");
	tokens.surfaceSunken = QStringLiteral("#e8e8e8");
	tokens.card = QStringLiteral("#ffffff");
	tokens.cardHover = QStringLiteral("#f0f6fc");
	tokens.cardSelected = QStringLiteral("#cce4f7");
	tokens.text = QStringLiteral("#000000");
	tokens.mutedText = QStringLiteral("#606060");
	tokens.border = QStringLiteral("#adadad");
	tokens.accent = QStringLiteral("#0078d7");
	tokens.accent2 = QStringLiteral("#2b88d8");
	tokens.focusRing = QStringLiteral("#0078d7");
	tokens.fontFamily = QStringLiteral("Segoe UI");
	tokens.monoFontFamily = QStringLiteral("Consolas");
	activeTheme().painter = &neutral;
	activeTheme().tokens = tokens;
}

const DeviceSkinPainter* DeviceSkinPainter::active()
{
	return activeTheme().painter;
}

const SkinTokens& DeviceSkinPainter::activeTokens()
{
	return activeTheme().tokens;
}

int DeviceSkinPainter::rowHeight(const QFontMetrics& fm, bool section) const
{
	// Devices carry two text lines (names + status sentence); sections one.
	if (section)
		return fm.height() + 14;
	return fm.height() * 2 + 18;
}

QRect DeviceSkinPainter::toggleRect(const QRect& rowRect) const
{
	// The whole left end of the row is the toggle's hit area (>= 40px).
	const int w = qMax(44, rowRect.height());
	return QRect(rowRect.left(), rowRect.top(), w, rowRect.height());
}

void DeviceSkinPainter::paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t) const
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor text(t.text);
	const QColor muted(t.mutedText);
	const QColor accent(t.accent);

	if (s.section)
	{
		painter.setPen(muted);
		QFont f = painter.font();
		f.setBold(true);
		painter.setFont(f);
		painter.drawText(rect.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, s.connection);
		painter.setPen(withAlpha(t.border, 160));
		painter.drawLine(QPointF(rect.left() + 8.5, rect.bottom() + 0.5), QPointF(rect.right() - 8.5, rect.bottom() + 0.5));
		painter.restore();
		return;
	}

	// Ground: hover raises the row toward cardHover; selection keeps an
	// accent edge on the left so the troubleshooting target stays readable.
	if (s.hover > 0.0)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(QColor(t.cardHover), 0.9 * s.hover));
		painter.drawRoundedRect(QRectF(rect).adjusted(2, 1, -2, -1), 6, 6);
	}
	if (s.selected)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(accent, 36));
		painter.drawRoundedRect(QRectF(rect).adjusted(2, 1, -2, -1), 6, 6);
		painter.setBrush(accent);
		painter.drawRoundedRect(QRectF(rect.left() + 2, rect.top() + 4, 3, rect.height() - 8), 1.5, 1.5);
	}

	// Toggle: a rounded well; checked fills with accent and draws the check.
	const QRect toggle = toggleRect(rect);
	QRectF box(0, 0, 18, 18);
	box.moveCenter(QRectF(toggle).center());
	if (s.pressed)
	{
		painter.translate(box.center());
		painter.scale(0.92, 0.92);
		painter.translate(-box.center());
	}
	painter.setPen(QPen(s.checked ? accent : withAlpha(muted, 180), 1.5));
	painter.setBrush(s.checked ? QBrush(accent) : QBrush(withAlpha(t.surfaceSunken, 160)));
	painter.drawRoundedRect(box, 4, 4);
	if (s.checked)
	{
		painter.setPen(QPen(QColor(t.surfaceSunken), 2));
		QPainterPath check;
		check.moveTo(box.left() + 4.5, box.center().y() + 0.5);
		check.lineTo(box.center().x() - 1, box.bottom() - 4.5);
		check.lineTo(box.right() - 4, box.top() + 5);
		painter.drawPath(check);
	}
	painter.resetTransform();

	// Names line + status line.
	const int textLeft = toggle.right() + 10;
	const QRect textRect(textLeft, rect.top() + 4, rect.width() - (textLeft - rect.left()) - 12, rect.height() - 8);
	const QFontMetrics fm(painter.font());
	painter.setPen(s.unavailable ? muted : text);
	QFont strong = painter.font();
	strong.setBold(true);
	painter.setFont(strong);
	const QString names = s.connection + QStringLiteral("  ·  ") + s.device;
	painter.drawText(QRect(textRect.left(), textRect.top(), textRect.width(), fm.height()),
		Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(names, Qt::ElideRight, textRect.width()));
	QFont normal = painter.font();
	normal.setBold(false);
	painter.setFont(normal);
	painter.setPen(s.checked && !s.installed ? accent : muted);
	painter.drawText(QRect(textRect.left(), textRect.bottom() - fm.height(), textRect.width(), fm.height()),
		Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(s.state, Qt::ElideRight, textRect.width()));

	painter.restore();
}

QSize DeviceSkinPainter::buttonSizeHint(const QFontMetrics& fm, const QString& text) const
{
	return QSize(fm.horizontalAdvance(text) + 44, qMax(34, fm.height() + 16));
}

void DeviceSkinPainter::paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& s, const SkinTokens& t) const
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor accent(t.accent);
	QColor ground = s.primary ? accent : QColor(t.surfaceRaised);
	if (!s.enabled)
		ground = withAlpha(ground, 70);
	else if (s.hover > 0.0)
		ground = mixColor(ground, QColor(t.cardHover), s.primary ? 0.0 : 0.6 * s.hover);
	if (s.primary && s.enabled && s.hover > 0.0)
		ground = mixColor(ground, QColor(t.text), 0.12 * s.hover);

	const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(s.primary ? withAlpha(accent, 200) : withAlpha(t.border, 200), 1));
	painter.setBrush(ground);
	painter.drawRoundedRect(r, 6, 6);
	if (s.focused && s.enabled)
	{
		painter.setPen(QPen(withAlpha(t.focusRing, 170), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 8, 8);
	}

	// Primary ink contrasts with the accent fill; secondary uses body text.
	QColor ink = s.primary
		? (skinColorIsDark(QColor(t.accent)) ? QColor(t.text) : QColor(t.surfaceSunken))
		: QColor(t.text);
	if (!s.enabled)
		ink = withAlpha(QColor(t.mutedText), 140);
	painter.setPen(ink);
	painter.drawText(rect, Qt::AlignCenter, s.text);
	painter.restore();
}

void DeviceSkinPainter::paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& s, const SkinTokens& t) const
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	if (s.hover > 0.0)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(QColor(t.cardHover), 0.8 * s.hover));
		painter.drawRoundedRect(QRectF(rect).adjusted(0, 2, 0, -2), 6, 6);
	}

	// Fold chevron: rotates right -> down as the panel opens.
	const QPointF c(rect.left() + 18, rect.center().y());
	painter.setPen(QPen(QColor(t.mutedText), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	QPainterPath chevron;
	if (s.open)
	{
		chevron.moveTo(c.x() - 4, c.y() - 2);
		chevron.lineTo(c.x(), c.y() + 3);
		chevron.lineTo(c.x() + 4, c.y() - 2);
	}
	else
	{
		chevron.moveTo(c.x() - 2, c.y() - 4);
		chevron.lineTo(c.x() + 3, c.y());
		chevron.lineTo(c.x() - 2, c.y() + 4);
	}
	painter.drawPath(chevron);

	painter.setPen(QColor(t.mutedText));
	painter.drawText(rect.adjusted(32, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, s.title);
	painter.restore();
}
