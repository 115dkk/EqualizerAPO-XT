/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass device selector: the device list as a glowing glass
	monitoring console. Constitution (pane formula, hairline rule, glow
	discipline): docs/skins/studio.md. Element mapping: an endpoint is a
	glass channel strip (the Editor cards' pane formula at list scale); the
	install toggle is a lit console pushbutton - a glass cap over an LED,
	dark in its socket until checked, then glowing the accent from within
	(stacked alpha strokes, never effects). Sections are etched captions
	under a luminous hairline; the selected strip - the troubleshooting
	target - wears the accent border glow and the left-edge signal lamp;
	unavailable strips are smoked glass (the light is off, never a
	warning). Buttons are glass keycaps; the disclosure is a glass tab
	whose fold light answers hover.
*/

#include "DeviceSkinPainter.h"

#include <QLinearGradient>
#include <QPainterPath>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// Sections: etched captions under a luminous hairline. The fold light
// (right -> down chevron) and the hairline both answer hover; the
// hairline's violet end lives only at the far side (rule 1).
void paintSectionRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t, bool dark)
{
	const QColor accent(t.accent);
	const QColor mutedInk(t.mutedText);
	const double warm = s.hover;

	const QPointF fold(rect.left() + 14.0, rect.center().y() + 0.5);
	QPainterPath chevron;
	if (s.expanded)
	{
		chevron.moveTo(fold.x() - 4.0, fold.y() - 2.0);
		chevron.lineTo(fold.x(), fold.y() + 3.0);
		chevron.lineTo(fold.x() + 4.0, fold.y() - 2.0);
	}
	else
	{
		chevron.moveTo(fold.x() - 2.0, fold.y() - 4.0);
		chevron.lineTo(fold.x() + 3.0, fold.y());
		chevron.lineTo(fold.x() - 2.0, fold.y() + 4.0);
	}
	painter.setBrush(Qt::NoBrush);
	const int foldBloom = qRound(70 * warm) + (s.expanded ? 26 : 0);
	if (foldBloom > 0)
	{
		painter.setPen(QPen(withAlpha(accent, qMin(150, foldBloom)), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPath(chevron);
	}
	painter.setPen(QPen(mixColor(mutedInk, accent, s.expanded ? 0.55 + 0.35 * warm : 0.45 * warm),
		1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawPath(chevron);

	QFont captionFont(t.fontFamily);
	captionFont.setPointSizeF(qMax(7.5, painter.font().pointSizeF() - 0.5));
	captionFont.setWeight(QFont::DemiBold);
	captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
	const QFontMetrics fmCaption(captionFont);
	painter.setFont(captionFont);
	painter.setPen(mixColor(mutedInk, QColor(t.text), 0.25 + 0.4 * warm));
	const int captionLeft = rect.left() + 28;
	const QString caption = fmCaption.elidedText(s.connection, Qt::ElideRight, rect.width() - captionLeft - 40);
	painter.drawText(QRect(captionLeft, rect.top(), fmCaption.horizontalAdvance(caption) + 4, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, caption);

	// The hairline light running from the caption to the strip's end.
	const double lineX0 = captionLeft + fmCaption.horizontalAdvance(caption) + 14.0;
	const double lineX1 = rect.right() - 8.0;
	if (lineX1 > lineX0 + 12.0)
	{
		const double lineY = rect.center().y() + 0.5;
		QLinearGradient hairline(lineX0, lineY, lineX1, lineY);
		hairline.setColorAt(0.0, withAlpha(t.border, dark ? 210 : 240));
		hairline.setColorAt(0.55, withAlpha(accent, qRound(46 + 74 * warm)));
		hairline.setColorAt(0.88, withAlpha(t.accent2, qRound(26 + 44 * warm)));
		hairline.setColorAt(1.0, withAlpha(t.accent2, 0));
		painter.setPen(QPen(QBrush(hairline), 1.0));
		painter.drawLine(QPointF(lineX0, lineY), QPointF(lineX1, lineY));
	}
}

// The install toggle: a lit console pushbutton. A raised glass cap
// (alpha fill, top reflection, hairline edge) over an LED that sits
// dark in its socket until checked, then glows the accent from within
// - stacked alpha circles for the lamp, border-hugging strokes for the
// light bleeding onto the console. Hover preheats the socket ember and
// climbs the checked lamp one step; pressing sinks the cap and flares
// the light so the press visibly answers.
void paintPushbutton(QPainter& painter, const QRectF& toggleArea, const QRectF& slab,
	const DeviceRowState& s, const SkinTokens& t, bool dark, double hover)
{
	const bool live = !s.unavailable;
	const QColor accent(t.accent);
	const QColor borderInk(t.border);

	const QPointF capCenter(toggleArea.left() + toggleArea.width() / 2.0 + 2.0, slab.center().y());
	const QRectF cap(qRound(capCenter.x() - 12.0) + 0.5, qRound(capCenter.y() - 12.0) + 0.5, 23.0, 23.0);

	painter.save();
	if (s.pressed)
	{
		// The cap sinks into the console.
		painter.translate(capCenter);
		painter.scale(0.93, 0.93);
		painter.translate(-capCenter);
		painter.translate(0.0, 0.6);
	}

	// Cap body: raised glass, faintly lit from beneath when checked.
	QColor capFill = mixColor(QColor(t.card), QColor(t.cardHover), dark ? 0.5 : 0.15);
	if (s.checked && live)
		capFill = mixColor(capFill, accent, 0.16 + 0.06 * hover);
	const QColor capEdge = s.checked && live
		? mixColor(borderInk, accent, 0.6)
		: mixColor(borderInk, accent, 0.35 * hover);
	painter.setPen(QPen(withAlpha(capEdge, live ? 220 : 130), 1.0));
	painter.setBrush(withAlphaF(capFill, live ? 0.92 : 0.45));
	painter.drawRoundedRect(cap, 8.0, 8.0);

	if (s.checked && live)
	{
		// The lamp bleeding onto the console: two hugging strokes.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(accent, qRound(80 + 50 * hover) + (s.pressed ? 40 : 0)), 1.0));
		painter.drawRoundedRect(cap, 8.0, 8.0);
		painter.setPen(QPen(withAlpha(accent, qRound(34 + 26 * hover) + (s.pressed ? 18 : 0)), 3.0));
		painter.drawRoundedRect(cap.adjusted(-1.5, -1.5, 1.5, 1.5), 9.5, 9.5);
	}

	if (live)
	{
		if (dark)
		{
			// Room light on the cap's top edge; pressed slides it off.
			const double reflY = cap.top() + 1.5;
			QLinearGradient reflection(cap.left(), reflY, cap.right(), reflY);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, s.pressed ? 22 : qRound(52 + 24 * hover)));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(cap.left() + 4.0, reflY), QPointF(cap.right() - 4.0, reflY));
		}
		else
		{
			// White glass: the cap's depth is a shade under its foot.
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(24, 32, 51, s.pressed ? 14 : qRound(24 + 12 * hover)));
			painter.drawRoundedRect(QRectF(cap.left() + 3.0, cap.bottom() - 1.5, cap.width() - 6.0, 1.5), 0.75, 0.75);
		}
	}

	// The LED under the glass cap.
	const QPointF led = cap.center();
	painter.setPen(Qt::NoPen);
	if (s.checked && live)
	{
		const int flare = s.pressed ? 40 : 0;
		painter.setBrush(withAlpha(accent, qRound(30 + 26 * hover) + flare));
		painter.drawEllipse(led, 9.0, 9.0);
		painter.setBrush(withAlpha(accent, qRound(70 + 40 * hover) + flare));
		painter.drawEllipse(led, 6.6, 6.6);
		painter.setBrush(withAlpha(accent, 235));
		painter.drawEllipse(led, 4.4, 4.4);
		painter.setBrush(mixColor(accent, QColor(255, 255, 255), 0.55 + 0.1 * hover));
		painter.drawEllipse(led, 2.3, 2.3);
	}
	else
	{
		// Dark socket; hover preheats an ember, pressing sparks it.
		painter.setBrush(withAlphaF(QColor(t.surfaceSunken), live ? (dark ? 0.85 : 0.9) : 0.5));
		painter.setPen(QPen(withAlpha(borderInk, live ? 200 : 120), 1.0));
		painter.drawEllipse(led, 4.6, 4.6);
		const int ember = live ? qRound(56 * hover) + (s.pressed ? 90 : 0) : 0;
		if (ember > 0)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(accent, qMin(200, ember)));
			painter.drawEllipse(led, 2.2, 2.2);
		}
	}

	painter.restore();
}

class StudioDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		// Two derived text lines per strip (names + status sentence) with
		// console breathing room; sections carry one etched caption.
		if (section)
			return fm.height() + 18;
		return fm.height() * 2 + 22;
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The whole left end of the strip belongs to the pushbutton.
		const int w = qMax(56, rowRect.height());
		return QRect(rowRect.left(), rowRect.top(), w, rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const bool dark = skinIsDark(t);
		const QColor accent(t.accent);
		const QColor textInk(t.text);
		const QColor mutedInk(t.mutedText);
		const QColor borderInk(t.border);

		if (s.section)
		{
			paintSectionRow(painter, rect, s, t, dark);
			painter.restore();
			return;
		}

		const bool live = !s.unavailable;
		const double hover = live ? s.hover : 0.0;

		// The tree paints its own selection fill across the row - including
		// the indentation strip left of the delegate's rect. The console has
		// exactly one selection voice (the slab's glow and lamp), so the
		// stage colour is laid back over the view's fill first.
		if (s.selected)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(t.background));
			painter.drawRect(QRectF(rect).adjusted(-12.0, 0.0, 0.0, 0.0));
		}

		// The strip: a glass slab floating on the deep stage.
		const QRectF slab = QRectF(rect).adjusted(2.5, 1.5, -2.5, -1.5);
		QPainterPath slabPath;
		slabPath.addRoundedRect(slab, 8.0, 8.0);

		const QColor fillBase(s.selected ? t.cardSelected : t.card);
		const QColor fillHover(s.selected ? t.cardSelected : t.cardHover);
		const QColor fill = mixColor(fillBase, fillHover, 0.85 * hover);
		const double fillAlpha = live ? (s.selected ? 0.92 : 0.82 + 0.08 * hover) : 0.38;
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(fill, fillAlpha));
		painter.drawPath(slabPath);

		painter.save();
		painter.setClipPath(slabPath);

		if (dark && live)
		{
			// Frost sheen settling down from the top edge (S1).
			QLinearGradient sheen(slab.topLeft(), QPointF(slab.left(), slab.top() + slab.height() * 0.45));
			sheen.setColorAt(0.0, QColor(255, 255, 255, qRound(15 + 9 * hover)));
			sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.fillPath(slabPath, sheen);
		}

		// The pane's thickness pooling at the bottom; on white glass this
		// shade carries the whole glass impression (S2).
		QLinearGradient depthShade(QPointF(slab.left(), slab.bottom() - slab.height() * 0.38), slab.bottomLeft());
		depthShade.setColorAt(0.0, QColor(0, 0, 0, 0));
		depthShade.setColorAt(1.0, dark ? QColor(0, 0, 0, live ? 52 : 28)
			: QColor(24, 32, 51, live ? qRound(26 + 10 * hover) : 14));
		painter.fillPath(slabPath, depthShade);

		// Hover: the glass catches light. A specular band travels the full
		// strip with the hover progress, brightest mid-transit and gone at
		// both ends, so the steady states stay clean glass.
		if (live && s.hover > 0.004 && s.hover < 0.996)
		{
			const double bandW = slab.width() * 0.32;
			const double bandX = slab.left() - bandW + s.hover * (slab.width() + 2.0 * bandW);
			const int gleam = qRound(qSin(M_PI * s.hover) * (dark ? 30 : 16));
			const QColor ray = dark ? QColor(255, 255, 255) : accent;
			QLinearGradient sweep(QPointF(bandX - bandW * 0.5, slab.top()), QPointF(bandX + bandW * 0.5, slab.bottom()));
			sweep.setColorAt(0.0, withAlpha(ray, 0));
			sweep.setColorAt(0.5, withAlpha(ray, gleam));
			sweep.setColorAt(1.0, withAlpha(ray, 0));
			painter.fillPath(slabPath, sweep);
		}

		// ...and where the light settles, it pools: a radial accent glow
		// rising from the slab's floor (the menu bar's pooling dialect),
		// holding as long as the pointer stays.
		if (live && hover > 0.004)
		{
			const QPointF poolCenter(slab.center().x(), slab.bottom());
			QRadialGradient pool(poolCenter, slab.width() * 0.34, poolCenter);
			pool.setColorAt(0.0, withAlpha(accent, qRound((dark ? 30 : 20) * hover)));
			pool.setColorAt(1.0, withAlpha(accent, 0));
			painter.fillPath(slabPath, pool);
		}

		if (dark && live)
		{
			// Centre-bright reflection caught just inside the top edge.
			const double reflY = slab.top() + 0.5;
			QLinearGradient reflection(slab.left(), reflY, slab.right(), reflY);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, qRound(56 + 28 * hover) + (s.selected ? 12 : 0)));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(slab.left() + 6.0, reflY), QPointF(slab.right() - 6.0, reflY));
		}

		if (s.selected && live)
		{
			// The troubleshooting target wears the signal lamp on its left
			// edge - the Editor's DSP-row grammar - blooming with hover.
			const double segment = 20.0;
			const double lampTop = slab.center().y() - segment / 2.0;
			const QColor fade = withAlpha(accent, 0);
			QLinearGradient bloom(0, lampTop - 4.0, 0, lampTop + segment + 4.0);
			bloom.setColorAt(0.0, fade);
			bloom.setColorAt(0.5, withAlpha(accent, qRound(70 + 50 * hover)));
			bloom.setColorAt(1.0, fade);
			painter.setPen(Qt::NoPen);
			painter.fillRect(QRectF(slab.left(), lampTop - 4.0, 5.5, segment + 8.0), bloom);
			QLinearGradient lamp(0, lampTop, 0, lampTop + segment);
			lamp.setColorAt(0.0, fade);
			lamp.setColorAt(0.5, withAlpha(accent, qMin(255, qRound(215 + 40 * hover))));
			lamp.setColorAt(1.0, fade);
			painter.fillRect(QRectF(slab.left(), lampTop, 2.5, segment), lamp);
		}

		painter.restore(); // clip

		// The slab's edge: selection glows accent (two hugging strokes fake
		// the halo); otherwise a hairline that warms toward the light.
		painter.setBrush(Qt::NoBrush);
		if (s.selected && live)
		{
			painter.setPen(QPen(withAlpha(accent, qRound(150 + 40 * hover)), 1.0));
			painter.drawRoundedRect(slab, 8.0, 8.0);
			painter.setPen(QPen(withAlpha(accent, qRound(40 + 26 * hover)), 3.0));
			painter.drawRoundedRect(slab.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);
		}
		else
		{
			const QColor edgeInk = mixColor(borderInk, accent, 0.55 * hover);
			painter.setPen(QPen(withAlpha(edgeInk, live ? 205 : 120), 1.0));
			painter.drawRoundedRect(slab, 8.0, 8.0);
		}

		// The console pushbutton.
		const QRect toggle = toggleRect(rect);
		paintPushbutton(painter, QRectF(toggle), slab, s, t, dark, hover);

		// Text zones. Names first: the connection is the strip's identity,
		// the device name follows as quieter ink on the same line.
		const int textLeft = toggle.right() + 10;
		QFont nameFont(t.fontFamily);
		nameFont.setPointSizeF(painter.font().pointSizeF());
		nameFont.setWeight(QFont::DemiBold);
		QFont deviceFont(t.fontFamily);
		deviceFont.setPointSizeF(nameFont.pointSizeF());
		QFont statusFont(t.fontFamily);
		statusFont.setPointSizeF(qMax(7.0, nameFont.pointSizeF() - 0.75));
		QFont microFont(t.monoFontFamily);
		microFont.setPointSizeF(qMax(6.5, nameFont.pointSizeF() - 2.5));
		microFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);

		const QFontMetrics fmName(nameFont);
		const QFontMetrics fmDevice(deviceFont);
		const QFontMetrics fmStatus(statusFont);
		const QFontMetrics fmMicro(microFont);

		const int lineGap = 2;
		const int blockH = fmName.height() + lineGap + fmStatus.height();
		const int nameTop = qRound(slab.top() + (slab.height() - blockH) / 2.0);
		const int statusTop = nameTop + fmName.height() + lineGap;
		int rightEdge = qRound(slab.right()) - 12;

		// Etched unit id at the strip's right end: the console's rack label.
		const QString unit = QStringLiteral("%1 %2")
			.arg(QLatin1String(s.input ? "IN" : "OUT"))
			.arg(s.index + 1, 2, 10, QLatin1Char('0'));
		const int unitW = fmMicro.horizontalAdvance(unit);
		painter.setFont(microFont);
		painter.setPen(withAlpha(mutedInk, live ? 150 : 90));
		painter.drawText(QRect(rightEdge - unitW, nameTop, unitW, fmName.height()),
			Qt::AlignRight | Qt::AlignVCenter, unit);
		rightEdge -= unitW + 12;

		// Experimental endpoints carry a switched-off warning-ink chip -
		// the ABS-chip grammar: ink and outline only, no lit fill.
		if (s.experimental)
		{
			const QString expText = QStringLiteral("EXP");
			const int chipW = fmMicro.horizontalAdvance(expText) + 12;
			const int chipH = fmMicro.height() + 4;
			const QRectF chip(rightEdge - chipW + 0.5, nameTop + (fmName.height() - chipH) / 2.0 + 0.5,
				chipW - 1, chipH - 1);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(t.warning, live ? 120 : 70), 1.0));
			painter.drawRoundedRect(chip, 8.0, 8.0);
			painter.setPen(withAlpha(t.warning, live ? 190 : 110));
			painter.drawText(chip, Qt::AlignCenter, expText);
			rightEdge -= chipW + 8;
		}

		const int defaultMarkerW = s.defaultDevice ? 16 : 0;
		const int nameAvail = rightEdge - textLeft - defaultMarkerW;
		const QString connText = fmName.elidedText(s.connection, Qt::ElideRight, nameAvail);
		const int connW = fmName.horizontalAdvance(connText);
		painter.setFont(nameFont);
		painter.setPen(live ? textInk : withAlpha(mutedInk, 200));
		painter.drawText(QRect(textLeft, nameTop, nameAvail, fmName.height()),
			Qt::AlignLeft | Qt::AlignVCenter, connText);

		int deviceLeft = textLeft + connW;
		if (s.defaultDevice)
		{
			// The default endpoint wears a small lit marker: the indicator
			// dot grammar (halo + core) beside its name.
			const QPointF dot(textLeft + connW + 9.0, nameTop + fmName.height() / 2.0 + 0.5);
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(accent, live ? qRound(40 + 40 * hover) : 30));
			painter.drawEllipse(dot, 5.0, 5.0);
			painter.setBrush(live ? accent : QColor(mutedInk));
			painter.drawEllipse(dot, 2.2, 2.2);
			deviceLeft += defaultMarkerW;
		}

		const int deviceAvail = rightEdge - deviceLeft - 10;
		if (deviceAvail > 12 && !s.device.isEmpty())
		{
			painter.setFont(deviceFont);
			painter.setPen(withAlpha(mutedInk, live ? 220 : 130));
			painter.drawText(QRect(deviceLeft + 10, nameTop, deviceAvail, fmName.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				fmDevice.elidedText(s.device, Qt::ElideRight, deviceAvail));
		}

		// The status sentence: a pending change (install or uninstall on OK)
		// takes the accent - the value the light is about to write.
		const bool pending = s.checked != s.installed;
		painter.setFont(statusFont);
		if (!live)
			painter.setPen(withAlpha(mutedInk, 140));
		else if (pending)
			painter.setPen(withAlpha(accent, 235));
		else
			painter.setPen(withAlpha(mutedInk, 230));
		const int statusAvail = qRound(slab.right()) - 12 - textLeft;
		painter.drawText(QRect(textLeft, statusTop, statusAvail, fmStatus.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			fmStatus.elidedText(s.state, Qt::ElideRight, statusAvail));

		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		return QSize(fm.horizontalAdvance(text) + 48, qMax(36, fm.height() + 18));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const bool dark = skinIsDark(t);
		const QColor accent(t.accent);
		const QRectF key = QRectF(rect).adjusted(2.5, 2.5, -2.5, -2.5);

		QFont keyFont(t.fontFamily);
		keyFont.setPointSizeF(painter.font().pointSizeF());
		keyFont.setWeight(QFont::DemiBold);
		painter.setFont(keyFont);

		if (!s.enabled)
		{
			// Dead glass: the key is present but its light is off.
			painter.setPen(QPen(withAlpha(t.border, 120), 1.0));
			painter.setBrush(withAlphaF(QColor(t.card), 0.40));
			painter.drawRoundedRect(key, 8.0, 8.0);
			painter.setPen(withAlpha(t.mutedText, 140));
			painter.drawText(rect, Qt::AlignCenter, s.text);
			painter.restore();
			return;
		}

		const double hover = s.hover;
		if (s.primary)
		{
			// The accept key glows accent from within: translucent accent
			// glass whose light climbs the ladder rest < hover < press.
			const double fillAlpha = (dark ? 0.36 : 0.22) + 0.12 * hover + (s.pressed ? 0.14 : 0.0);
			painter.setPen(QPen(withAlpha(accent, 220), 1.0));
			painter.setBrush(withAlphaF(accent, fillAlpha));
			painter.drawRoundedRect(key, 8.0, 8.0);

			// The halo: two border-hugging strokes, never effects.
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(accent, qRound(90 + 70 * hover) + (s.pressed ? 30 : 0)), 1.0));
			painter.drawRoundedRect(key.adjusted(-1.5, -1.5, 1.5, 1.5), 9.5, 9.5);
			painter.setPen(QPen(withAlpha(accent, qRound(30 + 34 * hover) + (s.pressed ? 20 : 0)), 2.5));
			painter.drawRoundedRect(key.adjusted(-1.0, -1.0, 1.0, 1.0), 9.0, 9.0);

			if (!s.pressed)
			{
				// Reflection along the cap's top edge; pressing sinks the
				// key and the room light slides off it.
				const double reflY = key.top() + 1.0;
				QLinearGradient reflection(key.left(), reflY, key.right(), reflY);
				reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
				reflection.setColorAt(0.5, QColor(255, 255, 255, qRound((dark ? 70 : 120) + 30 * hover)));
				reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
				painter.setPen(QPen(QBrush(reflection), 1.0));
				painter.drawLine(QPointF(key.left() + 5.0, reflY), QPointF(key.right() - 5.0, reflY));
			}
			painter.setPen(QColor(t.text));
		}
		else
		{
			// A quiet glass keycap: card glass that brightens one luminance
			// step under the pointer and sinks toward the accent when held.
			QColor fill = mixColor(QColor(t.card), QColor(t.cardHover), 0.8 * hover);
			if (s.pressed)
				fill = mixColor(fill, accent, 0.22);
			const QColor edge = mixColor(QColor(t.border), accent, s.pressed ? 0.85 : 0.5 * hover);
			painter.setPen(QPen(withAlpha(edge, 210), 1.0));
			painter.setBrush(withAlphaF(fill, 0.88));
			painter.drawRoundedRect(key, 8.0, 8.0);

			if (!s.pressed)
			{
				if (dark)
				{
					const double reflY = key.top() + 1.0;
					QLinearGradient reflection(key.left(), reflY, key.right(), reflY);
					reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
					reflection.setColorAt(0.5, QColor(255, 255, 255, qRound(40 + 34 * hover)));
					reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
					painter.setPen(QPen(QBrush(reflection), 1.0));
					painter.drawLine(QPointF(key.left() + 5.0, reflY), QPointF(key.right() - 5.0, reflY));
				}
				else
				{
					// White glass cannot brighten: the key's depth pools as
					// a shade along its bottom edge instead (S2).
					painter.setPen(Qt::NoPen);
					painter.setBrush(QColor(24, 32, 51, qRound(20 + 14 * hover)));
					painter.drawRoundedRect(QRectF(key.left() + 3.0, key.bottom() - 2.0, key.width() - 6.0, 1.5), 0.75, 0.75);
				}
			}
			painter.setPen(QColor(t.text));
		}

		if (s.focused)
		{
			painter.save();
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(t.focusRing, 170), 1.0));
			painter.drawRoundedRect(key.adjusted(-2.0, -2.0, 2.0, 2.0), 10.0, 10.0);
			painter.restore();
		}

		painter.drawText(rect, Qt::AlignCenter, s.text);
		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const bool dark = skinIsDark(t);
		const QColor accent(t.accent);
		const QColor mutedInk(t.mutedText);
		const double hover = s.hover;

		QFont titleFont(t.fontFamily);
		titleFont.setPointSizeF(qMax(7.5, painter.font().pointSizeF() - 0.5));
		titleFont.setWeight(QFont::DemiBold);
		const QFontMetrics fmTitle(titleFont);

		// The glass tab: sized to its fold light and caption, never wider
		// than the strip it sits on.
		const int chevronZone = 26;
		const int padX = 12;
		const int tabH = qMin(rect.height() - 4, fmTitle.height() + 14);
		const int tabW = qMin(rect.width() - 2,
			padX + chevronZone + fmTitle.horizontalAdvance(s.title) + padX);
		const QRectF tab(rect.left() + 0.5, rect.top() + qFloor((rect.height() - tabH) / 2.0) + 0.5,
			tabW, tabH);

		// Tab glass: a dim slot at rest, fitting its pane as the light
		// answers (open holds a mid step so the state stays readable).
		const double fillAlpha = 0.40 + 0.24 * hover + (s.open ? 0.14 : 0.0);
		const QColor fill = mixColor(QColor(t.card), QColor(t.cardHover), 0.7 * hover);
		const QColor edge = mixColor(QColor(t.border), accent, qMin(1.0, 0.45 * hover + (s.open ? 0.25 : 0.0)));
		painter.setPen(QPen(withAlpha(edge, 200), 1.0));
		painter.setBrush(withAlphaF(fill, fillAlpha));
		painter.drawRoundedRect(tab, 8.0, 8.0);

		if (dark && (s.open || hover > 0.01))
		{
			const double reflY = tab.top() + 1.0;
			QLinearGradient reflection(tab.left(), reflY, tab.right(), reflY);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, qRound(30 + 40 * hover) + (s.open ? 14 : 0)));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(tab.left() + 5.0, reflY), QPointF(tab.right() - 5.0, reflY));
		}

		// The fold: a chevron drawn as light (bloom under core) that turns
		// right -> down as the panel opens and warms under the pointer.
		const QPointF fold(tab.left() + padX + 5.0, tab.center().y());
		QPainterPath chevron;
		if (s.open)
		{
			chevron.moveTo(fold.x() - 4.0, fold.y() - 2.0);
			chevron.lineTo(fold.x(), fold.y() + 3.0);
			chevron.lineTo(fold.x() + 4.0, fold.y() - 2.0);
		}
		else
		{
			chevron.moveTo(fold.x() - 2.0, fold.y() - 4.0);
			chevron.lineTo(fold.x() + 3.0, fold.y());
			chevron.lineTo(fold.x() - 2.0, fold.y() + 4.0);
		}
		painter.setBrush(Qt::NoBrush);
		const int foldBloom = qRound(60 * hover) + (s.open ? 50 : 0);
		if (foldBloom > 0)
		{
			painter.setPen(QPen(withAlpha(accent, qMin(160, foldBloom)), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(chevron);
		}
		const QColor foldInk = s.open ? accent : mixColor(mutedInk, accent, 0.6 * hover);
		painter.setPen(QPen(foldInk, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPath(chevron);

		// The fold light answers hover: a ray runs out of the fold along
		// the tab's bottom edge, its violet end dying at the far side.
		const double reach = s.open ? 1.0 : hover;
		if (reach > 0.01)
		{
			const double rayY = tab.bottom() - 1.0;
			const double rayX0 = fold.x() + 6.0;
			const double rayX1 = rayX0 + (tab.right() - 8.0 - rayX0) * reach;
			const int rayAlpha = qRound(70 + 130 * (s.open ? qMax(0.45, hover) : hover));
			QLinearGradient ray(rayX0, rayY, rayX1, rayY);
			ray.setColorAt(0.0, withAlpha(accent, rayAlpha));
			ray.setColorAt(0.7, withAlpha(t.accent2, qRound(rayAlpha * 0.7)));
			ray.setColorAt(1.0, withAlpha(t.accent2, 0));
			// Bloom under core: the knob arc's stroke ladder laid flat.
			painter.setPen(QPen(QBrush(ray), 3.0, Qt::SolidLine, Qt::RoundCap));
			painter.setOpacity(0.4);
			painter.drawLine(QPointF(rayX0, rayY), QPointF(rayX1, rayY));
			painter.setOpacity(1.0);
			painter.setPen(QPen(QBrush(ray), 1.2, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(rayX0, rayY), QPointF(rayX1, rayY));
		}

		painter.setFont(titleFont);
		painter.setPen(s.open ? QColor(t.text) : mixColor(mutedInk, QColor(t.text), 0.6 * hover));
		painter.drawText(QRectF(tab.left() + padX + chevronZone, tab.top(),
			tab.width() - padX * 2 - chevronZone, tab.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			fmTitle.elidedText(s.title, Qt::ElideRight, tabW - padX * 2 - chevronZone));

		painter.restore();
	}
};
}

const DeviceSkinPainter* studioDeviceSkinPainter()
{
	static StudioDeviceSkin painter;
	return &painter;
}
