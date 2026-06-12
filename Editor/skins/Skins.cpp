/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "Skins.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QtMath>

#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/CurvedNodeRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"

namespace
{
// Common derived tokens shared by every skin.
void finishTokens(SkinTokens& t)
{
	t.surfaceRaised = t.cardHover;
	t.surfaceSunken = t.graph;
	t.graphGridMajor = t.border;
	if (t.graphGridMinor.isEmpty())
		t.graphGridMinor = t.border;
	t.focusRing = t.accent;
}

// ── Studio (glass, FabFilter-like) ──────────────────────────────────────────
class StudioSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("studio"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/studio_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static CurvedNodeRoutingRenderer renderer;
		return &renderer;
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 16;
		t.rowHeight = 40;
		t.channelGroupIndent = 18;
		t.channelGroupStyle = SkinTokens::GradientBar;
		t.badgeStyle = SkinTokens::ColorPill;
		if (dark)
		{
			t.background = QStringLiteral("#070A12");
			t.surface = QStringLiteral("#0D1322");
			t.card = QStringLiteral("#121A2C");
			t.cardHover = QStringLiteral("#182238");
			t.cardSelected = QStringLiteral("#1E3158");
			t.text = QStringLiteral("#E8EEFB");
			t.mutedText = QStringLiteral("#91A0BA");
			t.border = QStringLiteral("#26324A");
			t.graph = QStringLiteral("#060914");
			t.graphGridMinor = QStringLiteral("#26324A");
			t.accent = QStringLiteral("#5B8CFF");
			t.accent2 = QStringLiteral("#A66CFF");
		}
		else
		{
			t.background = QStringLiteral("#EEF2F8");
			t.surface = QStringLiteral("#F8FAFE");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#F3F6FC");
			t.cardSelected = QStringLiteral("#DDE8FF");
			t.text = QStringLiteral("#182033");
			t.mutedText = QStringLiteral("#66728A");
			t.border = QStringLiteral("#D8E0EF");
			t.graph = QStringLiteral("#F6F7FB");
			t.graphGridMinor = QStringLiteral("#D8E0EF");
			t.accent = QStringLiteral("#2F6BFF");
			t.accent2 = QStringLiteral("#8A4DFF");
		}
		finishTokens(t);
		return t;
	}
};

// ── Minimal ("The bank teller's terminal") helpers ──────────────────────────

// Screen point on a circle around center; degrees follow the shared knob
// convention (Qt-style angles, counter-clockwise from 3 o'clock, screen Y
// grows downward so sin is subtracted). Pass the negated clockwise sweep
// angle, exactly like ISkin.cpp's default renderer does.
QPointF minimalPointOnArc(const QPointF& center, double radius, double degrees)
{
	const double radians = qDegreesToRadians(degrees);
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}

// ANNEX K minimal: "the number is the control". The always-visible mono
// numeric is primary; beside it sits a small flat circle with a 1px indicator
// line and a hairline range arc, monochrome until dragged (accent while
// active). Bipolar knobs measure the indicator from a centre tick. Promoted
// legacy dials supply no valueText (their number lives in the adjacent spin
// box), so they render only the confirmation circle plus a ratio-derived
// position readout while hovered or dragged.
void paintMinimalKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor hairline(tokens.border);
	const QColor primary(state.enabled ? tokens.text : tokens.mutedText);
	const QColor secondary(tokens.mutedText);
	const QColor active(tokens.accent);
	const QColor indicatorColor = (state.enabled && state.dragging) ? active : primary;

	const bool hasNumber = !state.valueText.isEmpty();
	const double circleRadius = hasNumber ? 9.0 : 12.0;
	const double arcRadius = circleRadius + 4.0;

	QFont numberFont(tokens.monoFontFamily);
	numberFont.setBold(true);
	numberFont.setPointSizeF(9.0);

	QPointF circleCenter;
	QRectF numberRect;
	if (hasNumber)
	{
		// Number left (primary), confirmation circle beside it; the pair is
		// centred in the widget. Shrink the font instead of clipping when a
		// long value (e.g. "-100.0") meets a narrow widget.
		const double gap = 6.0;
		double available = rect.width() - 2.0 * arcRadius - gap - 4.0;
		double textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		while (textWidth > available && numberFont.pointSizeF() > 6.5)
		{
			numberFont.setPointSizeF(numberFont.pointSizeF() - 0.5);
			textWidth = QFontMetricsF(numberFont).horizontalAdvance(state.valueText);
		}
		const double pairWidth = textWidth + gap + 2.0 * arcRadius;
		const double left = rect.left() + (rect.width() - pairWidth) / 2.0;
		numberRect = QRectF(left, rect.top(), textWidth, rect.height());
		circleCenter = QPointF(left + textWidth + gap + arcRadius, QRectF(rect).center().y());
	}
	else
	{
		// Circle only; keep a constant bottom strip free for the hover/drag
		// readout so the circle does not jump when the readout appears.
		circleCenter = QPointF(QRectF(rect).center().x(), rect.top() + (rect.height() - 14.0) / 2.0);
	}

	// Hairline range arc: the full 270-degree value range, 1px.
	const QRectF arcRect(circleCenter.x() - arcRadius, circleCenter.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(arcRect, -135 * 16, -270 * 16);

	if (state.bipolar)
	{
		// Centre tick at 12 o'clock (the bipolar neutral) and a 1px deviation
		// arc measured from it: boost grows clockwise, cut counter-clockwise.
		// Unipolar knobs draw neither, so the two kinds read differently.
		painter.setPen(QPen(secondary, 1));
		painter.drawLine(minimalPointOnArc(circleCenter, arcRadius - 1.5, -270.0),
			minimalPointOnArc(circleCenter, arcRadius + 2.5, -270.0));
		const double deviationDegrees = 270.0 * (state.ratio - 0.5);
		painter.setPen(QPen(indicatorColor, 1));
		painter.drawArc(arcRect, -270 * 16, -qRound(deviationDegrees * 16.0));
	}

	// Small flat circle: flat fill, 1px border, hover = one background step.
	const QString fill = (state.enabled && (state.hovered || state.dragging)) ? tokens.cardHover : tokens.card;
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(QColor(fill));
	painter.drawEllipse(circleCenter, circleRadius, circleRadius);

	// 1px indicator line from the hub to the rim at the value angle.
	const double valueDegrees = -(135.0 + 270.0 * state.ratio);
	painter.setPen(QPen(indicatorColor, 1));
	painter.drawLine(circleCenter, minimalPointOnArc(circleCenter, circleRadius - 1.0, valueDegrees));

	if (hasNumber)
	{
		painter.setFont(numberFont);
		painter.setPen((state.enabled && state.dragging) ? active : primary);
		painter.drawText(numberRect, Qt::AlignVCenter | Qt::AlignLeft, state.valueText);
	}
	else if (state.enabled && (state.hovered || state.dragging))
	{
		// No supplied value text: show the dial position derived from ratio.
		// The real value sits in the adjacent spin box, so a percentage is the
		// only honest readout for log-scaled legacy dials.
		QFont readoutFont(tokens.monoFontFamily);
		readoutFont.setPointSizeF(7.5);
		painter.setFont(readoutFont);
		painter.setPen(state.dragging ? active : secondary);
		const QRectF readoutRect(rect.left(), rect.bottom() - 14.0, rect.width(), 14.0);
		painter.drawText(readoutRect, Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(state.ratio * 100.0)));
	}

	// Keyboard focus: a square hairline frame (radius 0 corner language).
	if (state.focused)
	{
		painter.setPen(QPen(QColor(tokens.focusRing), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
	}
}

// Leading type glyph for the line head, plain ASCII for the mapped types so
// every mono fallback font covers it. The glyph is shape information ("which
// kind of line is this"); the colour tag next to it stays the badge token.
QString minimalTypeGlyph(const QString& type)
{
	if (type == QStringLiteral("biquad"))
		return QStringLiteral("~");
	if (type == QStringLiteral("include"))
		return QStringLiteral(">>");
	if (type == QStringLiteral("vst"))
		return QStringLiteral("[]");
	if (type == QStringLiteral("copy"))
		return QStringLiteral("->");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	if (type == QStringLiteral("spacer"))
		return QString();
	// Unmapped commands keep the fixed-width column so line heads stay
	// aligned; the middle dot (U+00B7) deliberately carries no further
	// meaning. Built from a code point so the source stays pure ASCII (no
	// /utf-8 flag is set for MSVC).
	return QString(QChar(0x00B7));
}

// ── Minimal (Ableton-like terminal, monospace) ──────────────────────────────
class MinimalSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("minimal"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/precision_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static StepListRoutingRenderer renderer;
		return &renderer;
	}
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		paintMinimalKnob(painter, rect, state, tokens);
	}
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		// One flat line per command: 1px hairline box, square corners, and
		// state expressed as background-value steps only. Disabled rows fall
		// one step below the resting card; selection is the accent-value
		// background step; hover is exactly one step up from rest.
		const QString background = !info.enabled ? tokens.surface
			: (info.selected ? tokens.cardSelected : tokens.card);
		const QString borderColor = info.focused ? tokens.focusRing
			: (info.selected ? tokens.accent : tokens.border);
		QString style = QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: 0; }")
			.arg(background, borderColor);
		if (!info.selected)
		{
			style += QStringLiteral(" QFrame#FilterCardRow:hover { background: %1; }")
				.arg(!info.enabled ? tokens.card : tokens.cardHover);
		}
		return style;
	}
	QString cardHeaderStyle(const CommandRowInfo&, const SkinTokens&) const override
	{
		// No separate header plate: the row reads as a single text line, so
		// the header inherits the frame's background through transparency.
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0; }");
	}
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		Q_UNUSED(card);
		Q_UNUSED(body);
		// Leading type glyph at the line head. Only modern card rows carry a
		// header here; the Include/VST body editors and the frozen legacy
		// rows consult the hook with header == nullptr and stay untouched.
		if (header == nullptr)
			return;
		QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
		if (headerLayout == nullptr)
			return;
		// A commented-out line leads with the comment marker it actually
		// carries in the config file; the marker is information, not decor.
		const QString glyph = info.enabled ? minimalTypeGlyph(info.type) : QStringLiteral("#");
		if (glyph.isEmpty())
			return;
		QLabel* glyphLabel = new QLabel(glyph, header);
		glyphLabel->setObjectName(QStringLiteral("MinimalTypeGlyph"));
		glyphLabel->setAlignment(Qt::AlignCenter);
		glyphLabel->setMinimumWidth(18);
		headerLayout->insertWidget(0, glyphLabel);
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.accent = QStringLiteral("#3B82F6");
		t.fontFamily = QStringLiteral("DM Mono");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 0;
		t.rowHeight = 32;
		t.channelGroupIndent = 16;
		t.channelGroupStyle = SkinTokens::TreeLines;
		t.badgeStyle = SkinTokens::OutlineOnly;
		t.zebraStripe = true;
		if (dark)
		{
			t.background = QStringLiteral("#191919");
			t.surface = QStringLiteral("#1f1f1f");
			t.card = QStringLiteral("#262626");
			t.cardHover = QStringLiteral("#2c2c2c");
			t.cardSelected = QStringLiteral("#1f3554");
			t.text = QStringLiteral("#cccccc");
			t.mutedText = QStringLiteral("#777777");
			t.border = QStringLiteral("#3c3c3c");
			t.graph = QStringLiteral("#0e0e0e");
			t.graphGridMajor = QStringLiteral("#383838");
			t.graphGridMinor = QStringLiteral("#2c2c2c");
		}
		else
		{
			t.background = QStringLiteral("#F6F6F3");
			t.surface = QStringLiteral("#FFFFFF");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#F0F0EC");
			t.cardSelected = QStringLiteral("#E8F1FF");
			t.text = QStringLiteral("#202020");
			t.mutedText = QStringLiteral("#666660");
			t.border = QStringLiteral("#D2D2CC");
			t.graph = QStringLiteral("#FFFFFF");
			t.graphGridMajor = QStringLiteral("#D2D2CC");
			t.graphGridMinor = QStringLiteral("#E6E6E0");
		}
		finishTokens(t);
		return t;
	}
};

// ── Soft (macOS-like, rounded, soft shadows) ────────────────────────────────
class SoftSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("soft"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/soft_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static BlockChipRoutingRenderer renderer;
		return &renderer;
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.accent = QStringLiteral("#3B82F6");
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 18;
		t.rowHeight = 44;
		t.channelGroupIndent = 20;
		t.density = 2;
		t.channelGroupStyle = SkinTokens::SoftShadow;
		t.badgeStyle = SkinTokens::SoftPill;
		if (dark)
		{
			t.background = QStringLiteral("#171923");
			t.surface = QStringLiteral("#202433");
			t.card = QStringLiteral("#282D3E");
			t.cardHover = QStringLiteral("#30364A");
			t.cardSelected = QStringLiteral("#344065");
			t.text = QStringLiteral("#F2F4FA");
			t.mutedText = QStringLiteral("#A7AEC2");
			t.border = QStringLiteral("#3A4056");
			t.graph = QStringLiteral("#151925");
		}
		else
		{
			t.background = QStringLiteral("#F7F4EF");
			t.surface = QStringLiteral("#FFFDF9");
			t.card = QStringLiteral("#FFFFFF");
			t.cardHover = QStringLiteral("#FFF7EC");
			t.cardSelected = QStringLiteral("#EEF2FF");
			t.text = QStringLiteral("#28231F");
			t.mutedText = QStringLiteral("#786F67");
			t.border = QStringLiteral("#E9DED1");
			t.graph = QStringLiteral("#FFFAF3");
		}
		finishTokens(t);
		return t;
	}
};

// ── Rack (skeuomorphic 19" hardware) ────────────────────────────────────────
class RackSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("rack"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/rack_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static HardwarePatchbayRoutingRenderer renderer;
		return &renderer;
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 6;
		t.rowHeight = 36;
		t.channelGroupIndent = 16;
		t.channelGroupStyle = SkinTokens::DottedLine;
		t.badgeStyle = SkinTokens::WireframeBorder;
		t.accent = dark ? QStringLiteral("#F4B860") : QStringLiteral("#B66A00");
		t.accent2 = dark ? QStringLiteral("#5ED0A0") : QStringLiteral("#177A55");
		if (dark)
		{
			t.background = QStringLiteral("#0B0D0F");
			t.surface = QStringLiteral("#14181C");
			t.card = QStringLiteral("#1D2328");
			t.cardHover = QStringLiteral("#252B2F");
			t.cardSelected = QStringLiteral("#332718");
			t.text = QStringLiteral("#E6E0D4");
			t.mutedText = QStringLiteral("#9A9488");
			t.border = QStringLiteral("#3A4248");
			t.graph = QStringLiteral("#060807");
			t.graphGridMinor = QStringLiteral("#1F3A31");
		}
		else
		{
			t.background = QStringLiteral("#E7E2D8");
			t.surface = QStringLiteral("#F4EFE5");
			t.card = QStringLiteral("#FFFAEF");
			t.cardHover = QStringLiteral("#F7EEDC");
			t.cardSelected = QStringLiteral("#FCE8BD");
			t.text = QStringLiteral("#2B2721");
			t.mutedText = QStringLiteral("#746A5D");
			t.border = QStringLiteral("#C9BFAE");
			t.graph = QStringLiteral("#FFF7E6");
			t.graphGridMinor = QStringLiteral("#D6C4A6");
		}
		finishTokens(t);
		return t;
	}
};

// ── Matrix (signal matrix / instrument panel) ───────────────────────────────
class MatrixSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("matrix"); }
	QString qssResource(bool dark) const override
	{
		return QStringLiteral(":/skins/matrix_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	}
	IRoutingRenderer* routingRenderer() const override
	{
		static CrosspointMatrixRoutingRenderer renderer;
		return &renderer;
	}
	SkinTokens tokens(bool dark) const override
	{
		SkinTokens t;
		t.fontFamily = QStringLiteral("DM Sans");
		t.monoFontFamily = QStringLiteral("DM Mono");
		t.borderRadius = 10;
		t.rowHeight = 38;
		t.channelGroupIndent = 22;
		t.channelGroupStyle = SkinTokens::GradientBar;
		t.badgeStyle = SkinTokens::OutlineOnly;
		t.cardRailWidth = 3;
		t.accent = dark ? QStringLiteral("#22D3EE") : QStringLiteral("#008EAA");
		t.accent2 = dark ? QStringLiteral("#7CFFB2") : QStringLiteral("#0A8F57");
		if (dark)
		{
			t.background = QStringLiteral("#060B10");
			t.surface = QStringLiteral("#0B141C");
			t.card = QStringLiteral("#101B25");
			t.cardHover = QStringLiteral("#142432");
			t.cardSelected = QStringLiteral("#082B34");
			t.text = QStringLiteral("#DFF5FF");
			t.mutedText = QStringLiteral("#7FA0AE");
			t.border = QStringLiteral("#233443");
			t.graph = QStringLiteral("#041018");
			t.graphGridMinor = QStringLiteral("#183443");
		}
		else
		{
			t.background = QStringLiteral("#F0F6F8");
			t.surface = QStringLiteral("#FFFFFF");
			t.card = QStringLiteral("#F9FCFD");
			t.cardHover = QStringLiteral("#EDF7FA");
			t.cardSelected = QStringLiteral("#D7F8FF");
			t.text = QStringLiteral("#10242F");
			t.mutedText = QStringLiteral("#5F7782");
			t.border = QStringLiteral("#D4E2E8");
			t.graph = QStringLiteral("#F9FCFD");
			t.graphGridMinor = QStringLiteral("#D4E2E8");
		}
		finishTokens(t);
		return t;
	}
};

StudioSkin g_studio;
MinimalSkin g_minimal;
SoftSkin g_soft;
RackSkin g_rack;
MatrixSkin g_matrix;
}

namespace Skins
{
QList<ISkin*> all()
{
	return { &g_studio, &g_minimal, &g_soft, &g_rack, &g_matrix };
}

ISkin* byId(const QString& id)
{
	QString resolved = id;
	if (resolved == QStringLiteral("glassy"))
		resolved = QStringLiteral("studio");
	else if (resolved == QStringLiteral("industrial"))
		resolved = QStringLiteral("rack");

	for (ISkin* skin : all())
		if (skin->id() == resolved)
			return skin;
	return &g_studio;
}
}
