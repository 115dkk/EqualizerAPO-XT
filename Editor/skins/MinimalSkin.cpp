/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Minimal skin, split out of Skins.cpp (audit #109 F005). This is a verbatim
// move of the helpers and the class; behaviour is unchanged. The file-scope
// instance is exposed through minimalSkin() so Skins::all() can assemble the
// roster without a central definition list.

#include "Skins.h"

#include <QAction>
#include <QComboBox>
#include <QDial>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

// Studio's S3 band-colour law maps BiQuad filter types onto hue families.
#include "filters/BiQuad.h"

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/RackChrome.h"
#include "Editor/skins/pickers/StudioFilterPicker.h"
#include "Editor/skins/pickers/MinimalFilterPicker.h"
#include "Editor/skins/pickers/SoftFilterPicker.h"
#include "Editor/skins/pickers/RackFilterPicker.h"
#include "Editor/skins/pickers/MatrixFilterPicker.h"
#include "Editor/skins/cards/MinimalReferenceCardView.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"
#include "SkinSupport.h"

namespace
{
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

// ANNEX K minimal: "the number is the control; the knob is confirmation"
// (N2). The figure is the brightest ink in the row - painted here when the
// widget supplies valueText, living in the adjacent ValueScrubBox (promoted
// by precision_*.qss) for the row dials, which supply none. The knob itself
// is a hairline instrument: a 1px 270-degree range arc, a travelled arc in
// text ink and a radial cursor tick at the value angle - no filled disc, no
// hub. Unipolar dials measure travel from the range start; bipolar dials
// measure a deviation arc from a fixed 12 o'clock detent tick (boost
// clockwise, cut counter-clockwise), so the two kinds part at a glance (X3)
// and 0 dB reads as "cursor on the detent, no deviation". Monochrome until
// dragged; dragging turns the travelled ink accent (active-state law).
void paintMinimalKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor hairline(tokens.border);
	const QColor secondary(tokens.mutedText);
	const QColor active(tokens.accent);
	// The promoted figure sits one brightness step above body text: white on
	// the dark console, full black on the light paper. Mode is read off the
	// background's value because SkinTokens carries no dark flag.
	const bool darkMode = QColor(tokens.background).lightness() < 128;
	const QColor promoted(!state.enabled ? secondary
		: (darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0)));
	const QColor travelled = !state.enabled ? secondary
		: (state.dragging ? active : QColor(tokens.text));

	const bool hasNumber = !state.valueText.isEmpty();
	const double arcRadius = hasNumber ? 9.0 : 12.0;

	QFont numberFont(tokens.monoFontFamily);
	numberFont.setBold(true);
	numberFont.setPointSizeF(9.0);

	QPointF arcCenter;
	QRectF numberRect;
	if (hasNumber)
	{
		// Number left (primary), confirmation arc beside it; the pair is
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
		arcCenter = QPointF(left + textWidth + gap + arcRadius, QRectF(rect).center().y());
	}
	else
	{
		// Arc only; keep a constant bottom strip free for the hover/drag
		// readout so the instrument does not jump when the readout appears.
		arcCenter = QPointF(QRectF(rect).center().x(), rect.top() + (rect.height() - 14.0) / 2.0);
	}

	// Hairline range arc: the full 270-degree travel, 1px, open across the
	// bottom dead zone like every knob in the product.
	const QRectF arcRect(arcCenter.x() - arcRadius, arcCenter.y() - arcRadius, arcRadius * 2.0, arcRadius * 2.0);
	painter.setPen(QPen(hairline, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(arcRect, -135 * 16, -270 * 16);

	if (state.bipolar)
	{
		// Fixed detent tick at 12 o'clock and a 1px deviation arc measured
		// from it: boost grows clockwise, cut counter-clockwise. On the
		// detent the deviation vanishes and only the tick remains - the
		// honest "0 dB".
		painter.setPen(QPen(secondary, 1));
		painter.drawLine(minimalPointOnArc(arcCenter, arcRadius - 2.5, -270.0),
			minimalPointOnArc(arcCenter, arcRadius + 2.5, -270.0));
		const double deviationDegrees = 270.0 * (state.ratio - 0.5);
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -270 * 16, -qRound(deviationDegrees * 16.0));
	}
	else
	{
		// Unipolar: the travelled range fills from the arc's start. No detent
		// tick, no centre origin - the two kinds cannot be confused.
		painter.setPen(QPen(travelled, 1));
		painter.drawArc(arcRect, -135 * 16, -qRound(270.0 * state.ratio * 16.0));
	}

	// Radial cursor tick crossing the range arc at the value angle.
	const double valueDegrees = -(135.0 + 270.0 * state.ratio);
	painter.setPen(QPen(travelled, 1));
	painter.drawLine(minimalPointOnArc(arcCenter, arcRadius - 3.0, valueDegrees),
		minimalPointOnArc(arcCenter, arcRadius + 3.0, valueDegrees));

	if (hasNumber)
	{
		painter.setFont(numberFont);
		painter.setPen((state.enabled && state.dragging) ? active : promoted);
		painter.drawText(numberRect, Qt::AlignVCenter | Qt::AlignLeft, state.valueText);
	}
	else if (state.enabled && (state.hovered || state.dragging))
	{
		// No supplied value text: show the dial position derived from ratio.
		// The real value sits in the adjacent scrub box, so a percentage is
		// the only honest readout for log-scaled legacy dials.
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
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		// The add-filter dropdown as a numbered terminal index; see
		// MinimalFilterPicker.h for the design.
		return new MinimalFilterPickerView(parent);
	}
	// The reference bodies (Include / Convolution / MultiConvolution / VST) as
	// one line of type: payload in the brightest ink, location and readout as
	// muted print, the broken reference as an inverted MISSING block and the
	// actions as engraved command words; see MinimalReferenceCardView.h.
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new MinimalReferenceCardView(kind, parent);
	}
	// The toolbar is the terminal's command line: all type and one hairline.
	// The neutral default keeps the shared stroke icons on the actions so the
	// File menu (which shares the QActions) stays modern; the toolbar buttons
	// themselves drop the pictures and show the command words instead -
	// precision_*.qss uppercases and letter-spaces them and walks their states
	// up the value ladder (hover = one background step, pressed = the
	// inverted block from the picker's cursor grammar). Both calls set
	// absolute state, so re-running on every skin/dark switch is idempotent.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
	{
		if (toolBar == nullptr)
			return;
		ISkin::styleMainToolbar(toolBar, tokens);
		toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
			t.cardSelected = QStringLiteral("#2A4878");
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
}

ISkin* minimalSkin()
{
	static MinimalSkin instance;
	return &instance;
}
