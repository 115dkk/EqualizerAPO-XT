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
#include "SkinThemeData.h"

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

// The GraphicEQ response plot as this skin's instrument: a measurement
// record on the console (dark) or the printed sheet (light). Ground is the
// graph token, the grid is the faintest 1px hairlines, and every straight
// line is drawn crisp with antialiasing off; only the response curve keeps
// its antialiasing, because the curve is data. The curve itself is a 1px
// body-ink hairline with no fill and no accent (accent only while active).
// Nodes are square hairline ticks walking the value ladder: rest is a
// ground-punched hairline square, hover fills the square one background
// value step (cardHover), selection is the inverted block - the picker's
// bluntest cursor - and only the selected node under the pointer (the one
// being dragged) wears the accent block. The keyboard cursor gets the
// square accent hairline frame while the widget holds focus. Band-locked
// layouts (15/31) hang 1px hairline stems from the 0 dB rule - no bar
// fills, ink spent on decoration is waste. Disabled drops the data inks
// (curve, nodes, 0 dB) one brightness step to secondary; no strikeout, no
// warning colour.
void paintMinimalGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens)
{
	const QColor ground(tokens.graph);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor secondary(tokens.mutedText);
	const QColor bodyInk(state.enabled ? QColor(tokens.text) : QColor(tokens.mutedText));
	const QColor accent(tokens.accent);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(state.rect, ground);

	// Axis labels: secondary-ink mono print in the margins.
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(7.5);
	painter.setFont(labelFont);

	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());

	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		const int x = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(x, plotTop, x, plotBottom);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(x - 24, plotBottom + 2, 48, state.rect.bottom() - plotBottom - 2),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		const int y = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(plotLeft, y, plotRight, y);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(state.rect.left(), y - 8, plotLeft - state.rect.left() - 4, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}
	}

	// The 0 dB rule: the one full-strength straight line, body ink 1px.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		painter.setPen(QPen(bodyInk, 1));
		painter.drawLine(plotLeft, qRound(state.zeroY), plotRight, qRound(state.zeroY));
	}

	// Band-locked stems: 1px hairline verticals from the 0 dB rule to each
	// band level, in secondary ink so the response stays the brightest line.
	if (state.bandLocked)
	{
		painter.setPen(QPen(secondary, 1));
		const int base = qRound(qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom()));
		for (const QPointF& node : state.nodePositions)
			painter.drawLine(qRound(node.x()), base, qRound(node.x()), qRound(node.y()));
	}

	// The response: data, so it keeps its antialiasing. 1px, no fill.
	if (state.curve.size() >= 2)
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(bodyInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(state.curve);
		painter.setRenderHint(QPainter::Antialiasing, false);
	}

	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const int x = qRound(state.nodePositions.at(i).x());
		const int y = qRound(state.nodePositions.at(i).y());
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;
		if (selected)
		{
			// The inverted block. While a drag is live the dragged node is the
			// selected one under the pointer, and only it turns accent (the
			// active-state law); a disabled row's stored selection dims to the
			// secondary block - still inverted, no longer a live cursor.
			const QColor block = !state.enabled ? secondary : (hovered ? accent : bodyInk);
			painter.fillRect(QRect(x - 3, y - 3, 7, 7), block);
		}
		else
		{
			// A square hairline tick punched into the ground; hover fills it
			// exactly one background value step.
			painter.setPen(QPen(bodyInk, 1));
			painter.setBrush(hovered ? QColor(tokens.cardHover) : ground);
			painter.drawRect(x - 3, y - 3, 6, 6);
		}
		if (state.focused && state.focusedNode == i)
		{
			// The keyboard cursor: the square accent hairline frame.
			painter.setPen(QPen(QColor(tokens.focusRing), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(x - 5, y - 5, 10, 10);
		}
	}

	// Cursor readout: one secondary-ink mono line, top right.
	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setPen(secondary);
		painter.setFont(labelFont);
		painter.drawText(QRectF(state.plotRect).adjusted(0, 2, -4, 0), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The frame: one square 1px hairline, exactly like the analysis graph;
	// keyboard focus swaps it for the accent hairline (focus grammar).
	painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : QColor(tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
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
		// SkinThemeData keeps the historical precision_* file names.
		return SkinThemeData::qssResource(id(), dark);
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
	// The GraphicEQ response plot as a measurement record on the console/
	// paper; see paintMinimalGraphicEqPlot above for the full grammar.
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		paintMinimalGraphicEqPlot(painter, state, tokens);
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
		// A raw line (a bare note, or a programmatic command like If/EndIf
		// the editor does not model) is source text, and this skin is the
		// source's native register: print it bare on the body strip. The
		// shared raw card's inline chrome (sunken input ground in a hairline
		// box) says "foreign object"; here the honest presentation is plain
		// ink - no box, no input ground (the comment card's law, but in body
		// ink because the line is live source, not dead code). A
		// commented-out line drops to secondary ink with the rest of the
		// row. The '>_' marker keeps its shared muted-mono inline style as
		// the raw-mode tag. Rows are rebuilt on skin/theme switches, so
		// construction-time token values stay current.
		if (info.type == QStringLiteral("text") && body != nullptr)
		{
			const SkinTokens& tk = SkinManager::instance()->tokens();
			if (QLabel* rawText = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				rawText->setStyleSheet(QStringLiteral("QLabel#FilterCardRawText { background: transparent; color: %1; border: 0; border-radius: 0; padding: 2px 0; font-family: \"%2\"; }")
					.arg(info.enabled ? tk.text : tk.mutedText, tk.monoFontFamily));
			}
		}
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
	// The trailing add row is the terminal's input prompt line: "+ ADD
	// FILTER" as an uppercase tracked mono caption inside a 1px hairline
	// slot - the engraved-command grammar (BROWSE/LOCATE) at line scale.
	// State keeps to the value ladder: rest is the bare hairline box on the
	// list ground, hover lifts the ground exactly one value step, keyboard
	// focus is the square accent hairline frame and the press instant is
	// the inverted block (the command canon's cursor). No dashes: a dashed
	// hairline means "no verified substance" in this skin's chip grammar,
	// and this slot is a real command.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		QColor ink(tokens.mutedText);
		QColor edge(tokens.border);
		if (state.pressed)
		{
			painter.fillRect(rect, QColor(tokens.text));
			ink = QColor(tokens.surface);
			edge = QColor(tokens.text);
		}
		else if (state.hovered)
		{
			// One ground step plus the comment card's hover law: the caption
			// ink lifts to body brightness because the line acts on click.
			painter.fillRect(rect, QColor(tokens.surface));
			ink = QColor(tokens.text);
		}
		if (state.focused && !state.pressed)
			edge = QColor(tokens.focusRing);
		painter.setPen(QPen(edge, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect.adjusted(0, 0, -1, -1));

		QFont font(tokens.monoFontFamily);
		font.setPointSizeF(9.0);
		font.setWeight(QFont::Bold);
		font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(font);
		painter.setPen(ink);
		painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
			QStringLiteral("+ ") + state.label.toUpper());
	}
	// The first-boundary seam: a text editor's insert line. One 1px accent
	// hairline rules the boundary and an ASCII '+' in a square hairline
	// cell sits at the line head; the press instant fills the cell with
	// the accent block (the armed-flag grammar of the menu and checkbox
	// indicators). No curvature, no glow, no disc. The widget only shows
	// itself while hovered, so at rest nothing is painted anywhere.
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		if (!state.hovered && !state.pressed)
			return;
		const QColor accent(tokens.accent);
		const int centerY = rect.center().y();
		const int side = qMin(rect.height(), 12);
		const QRect cell(rect.left(), centerY - side / 2, side, side);

		painter.setPen(QPen(accent, 1));
		painter.setBrush(state.pressed ? QBrush(accent) : Qt::NoBrush);
		painter.drawRect(cell.adjusted(0, 0, -1, -1));
		painter.drawLine(cell.right() + 5, centerY, rect.right(), centerY);

		QFont font(tokens.monoFontFamily);
		font.setPixelSize(qMax(7, side - 3));
		font.setWeight(QFont::Bold);
		painter.setFont(font);
		painter.setPen(state.pressed ? QColor(tokens.background) : accent);
		painter.drawText(cell, Qt::AlignCenter, QStringLiteral("+"));
	}
	// The token table lives in SkinThemeData (shared with satellite tools);
	// this class keeps only behaviour.
	SkinTokens tokens(bool dark) const override
	{
		return SkinThemeData::tokens(id(), dark);
	}
};
}

ISkin* minimalSkin()
{
	static MinimalSkin instance;
	return &instance;
}
