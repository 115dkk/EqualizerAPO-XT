/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Mechanical paint helpers shared by the skin TUs (skins, pickers,
	reference cards, routing renderers). Every function here used to exist as
	near-identical per-file copies (withAlpha alone had nine); none of them
	carries a design decision, so sharing them cannot breach the
	differentiation gate - a skin's grammar lives in what it draws, not in
	how a QColor gets its alpha. Additions must stay design-free; per-skin
	recipes are only admitted when several TUs of the SAME skin need them and
	are marked as off-limits to the neighbours.

	Header-only on purpose: SkinThemeData/DeviceSelector-style satellite
	consumers can include it without linking anything.
*/

#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QtMath>

#include "Editor/SkinTokens.h"

// The skin hooks receive tokens but no mode flag; a colour's luminance is the
// unambiguous proxy (every dark ground in the five palettes is deep, every
// light one near-white).
inline bool skinColorIsDark(const QColor& color)
{
	return color.lightness() < 128;
}

inline bool skinIsDark(const SkinTokens& tokens)
{
	return skinColorIsDark(QColor(tokens.background));
}

inline QColor withAlpha(QColor color, int alpha)
{
	color.setAlpha(alpha);
	return color;
}

inline QColor withAlpha(const QString& hex, int alpha)
{
	return withAlpha(QColor(hex), alpha);
}

inline QColor withAlphaF(QColor color, double alpha)
{
	color.setAlphaF(alpha);
	return color;
}

// Linear blend between two colours; t = 0 returns a, t = 1 returns b. The
// skins fake elevation steps and pastel arcs by mixing token colours instead
// of introducing palette entries.
inline QColor mixColor(const QColor& a, const QColor& b, double t)
{
	return QColor(
		qRound(a.red() + (b.red() - a.red()) * t),
		qRound(a.green() + (b.green() - a.green()) * t),
		qRound(a.blue() + (b.blue() - a.blue()) * t));
}

// QSS colour strings for inline rules built from token colours.
inline QString cssRgba(const QColor& color, double alpha)
{
	return QStringLiteral("rgba(%1, %2, %3, %4)")
		.arg(color.red()).arg(color.green()).arg(color.blue())
		.arg(alpha, 0, 'f', 2);
}

inline QString cssRgba(const QString& hex, double alpha)
{
	return cssRgba(QColor(hex), alpha);
}

inline QString cssColor(const QColor& color)
{
	return color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

// Screen point on a circle around center. Qt-style angles: counter-clockwise
// from 3 o'clock, and screen Y grows downward, so sin is subtracted. Pass the
// negated clockwise sweep angle, exactly like the knob painters always did.
inline QPointF skinArcPoint(const QPointF& center, double radius, double degrees)
{
	const double radians = qDegreesToRadians(degrees);
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}

// Soft's pastel-shelf recipe (constitution-cited: "파스텔은 토큰 혼합으로").
// Shared here only because SoftSkin and SoftReferenceCardView both need it;
// the other four skins must not adopt it - pastel derivation IS Soft's
// grammar (differentiation gate).
inline QColor softPastelize(const QColor& base, bool dark)
{
	const double hue = base.hslHueF() < 0.0 ? 215.0 / 360.0 : base.hslHueF();
	const double saturation = qMin(base.hslSaturationF(), dark ? 0.50 : 0.55);
	return QColor::fromHslF(hue, saturation, dark ? 0.62 : 0.60);
}
