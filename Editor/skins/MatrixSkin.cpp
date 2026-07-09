/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Matrix skin, split out of Skins.cpp (audit #109 F005). This is a verbatim
// move of the helpers and the class; behaviour is unchanged. The file-scope
// instance is exposed through matrixSkin() so Skins::all() can assemble the
// roster without a central definition list.

#include "Skins.h"

#include <QAction>
#include <QComboBox>
#include <QDial>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
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
#include "Editor/skins/cards/MatrixReferenceCardView.h"
#include "Editor/skins/pickers/StudioFilterPicker.h"
#include "Editor/skins/pickers/MinimalFilterPicker.h"
#include "Editor/skins/pickers/SoftFilterPicker.h"
#include "Editor/skins/pickers/RackFilterPicker.h"
#include "Editor/skins/pickers/MatrixFilterPicker.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "Editor/widgets/routing/StepListRoutingRenderer.h"
#include "Editor/widgets/routing/BlockChipRoutingRenderer.h"
#include "Editor/widgets/routing/HardwarePatchbayRoutingRenderer.h"
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Matrix (signal-routing matrix / departure board) ────────────────────────
//
// Constitution: an airport departure board / broadcast routing matrix.
// Everything aligns to a grid; numeric values are monospace; 1px rules; rows
// behave like cells with coordinates. Traffic-light colours (green/amber/red)
// are reserved for status and never used decoratively. Hover highlights the
// row band and the coordinate-column band (crosspoint feel). Disabled rows get
// a dashed border. Knobs are rotary encoders with a stepped LED ring and a
// boxed mono numeric cell as the authoritative reading.

namespace MatrixMetrics
{
// The grid the whole skin aligns to (card grid pitch, coordinate column).
constexpr int gridPitch = 24;
// Width of the coordinate-column band (expand + number + type cells of the
// header) used by the crosspoint hover highlight.
constexpr int coordinateBandWidth = 120;
// Left inset of the card content: 3px status rail (border-left) + 1px gutter.
constexpr int railInset = 4;
// Height of the boxed numeric readout cell under a card knob.
constexpr int knobCellHeight = 16;
}

namespace
{
// Point on the 270-degree value arc; fraction 0 is bottom-left (7:30), 0.5 is
// 12 o'clock, 1 is bottom-right (4:30). Same sweep as the shared default
// knob; the trig itself lives in SkinPaint.h.
QPointF matrixRadialPoint(const QPointF& center, double radius, double fraction)
{
	return skinArcPoint(center, radius, -(135.0 + 270.0 * fraction));
}

// Bus designation of a command type (M2): the row coordinate speaks the same
// letter-plus-number grammar as the picker's board coordinates (A1..G8), with
// the letter mirroring the mono type code the badge cell already shows
// (BQUAD -> B, INC -> I, VST -> V, ...). The letter is a designation, not an
// identifier - two types may share one, exactly like two flights share a
// carrier letter; uniqueness stays with the line number, which never
// renumbers while the board is scanned.
QString matrixBusLetter(const QString& type)
{
	if (type == QStringLiteral("biquad"))
		return QStringLiteral("B");
	if (type == QStringLiteral("preamp"))
		return QStringLiteral("P");
	if (type == QStringLiteral("delay") || type == QStringLiteral("device"))
		return QStringLiteral("D");
	if (type == QStringLiteral("graphiceq"))
		return QStringLiteral("G");
	if (type == QStringLiteral("copy") || type == QStringLiteral("channel") || type == QStringLiteral("convolution"))
		return QStringLiteral("C");
	if (type == QStringLiteral("include"))
		return QStringLiteral("I");
	if (type == QStringLiteral("vst"))
		return QStringLiteral("V");
	if (type == QStringLiteral("stage"))
		return QStringLiteral("S");
	if (type == QStringLiteral("loudness"))
		return QStringLiteral("L");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	// Unrecognized raw lines: a remark entry on the board.
	return QStringLiteral("R");
}

// Per-row caption strip (M2): the picker footer's grammar imported into the
// card. A sunken board line fixed under the card body echoes the row's raw
// spec ("> Filter: ON PK ...") next to the row's board coordinate, exactly
// like the picker footer echoes the line an engaged coordinate would insert.
// At rest the readout idles in muted ink; while the row crosspoint is hovered
// the echo lights - marker and coordinate in accent, spec in full ink. The
// strip needs no event machinery: the frame's :hover QSS rule already forces
// a frame repaint on enter/leave (the same trigger the painted column band
// uses), which redraws this child, and the gallery's WA_UnderMouse hover
// equivalent drives it the same way. It replaces the shared raw-preview
// strip for this skin (tokens().showRawPreview = false), so the row spends
// the same vertical budget on a line that follows the board's grammar.
class MatrixRowCaption : public QWidget
{
public:
	MatrixRowCaption(QWidget* card, QLabel* specSource, QLabel* coordinateSource)
		: QWidget(card), specSource(specSource), coordinateSource(coordinateSource)
	{
		setObjectName(QStringLiteral("MatrixRowCaption"));
		// The strip is a readout, never a control; clicks fall through.
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFixedHeight(18);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const SkinTokens& tokens = SkinManager::instance()->tokens();
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, false);

		QWidget* card = parentWidget();
		// The dynamic property is kept current by FilterCardRow's restyles;
		// before the first restyle it is simply unset, which reads enabled.
		const QVariant enabledProperty = card != nullptr ? card->property("filterEnabled") : QVariant();
		const bool enabled = !enabledProperty.isValid() || enabledProperty.toBool();
		const bool lit = enabled && card != nullptr && card->underMouse();

		// Sunken board line under a 1px top rule - the picker footer's
		// construction.
		painter.fillRect(rect(), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(0, 0, width() - 1, 0);

		QFont mono(tokens.monoFontFamily);
		mono.setPointSizeF(7.5);
		painter.setFont(mono);
		const QFontMetrics metrics(mono);

		QColor idleInk(tokens.mutedText);
		if (!enabled)
			idleInk.setAlpha(120);
		const QColor accent(tokens.accent);
		const int pad = 10;

		// Board coordinate readout on the right.
		const QString coordinate = coordinateSource != nullptr ? coordinateSource->text() : QString();
		const int coordinateWidth = metrics.horizontalAdvance(coordinate);
		painter.setPen(lit ? accent : idleInk);
		painter.drawText(QRect(width() - pad - coordinateWidth, 0, coordinateWidth, height()),
			Qt::AlignVCenter | Qt::AlignLeft, coordinate);

		// "> <raw line>" spec echo. The raw-preview label keeps its text
		// current on every model rebuild even while hidden, so it doubles as
		// the live source of the row's raw spec.
		QString spec = specSource != nullptr ? specSource->text() : QString();
		if (spec.startsWith(QStringLiteral("Raw")))
			spec = spec.mid(3).trimmed();
		const QString marker = QStringLiteral("> ");
		painter.drawText(QRect(pad, 0, width(), height()), Qt::AlignVCenter | Qt::AlignLeft, marker);
		const int specX = pad + metrics.horizontalAdvance(marker);
		const int specAvail = width() - pad - coordinateWidth - 12 - specX;
		painter.setPen(lit ? QColor(tokens.text) : idleInk);
		painter.drawText(QRect(specX, 0, qMax(0, specAvail), height()), Qt::AlignVCenter | Qt::AlignLeft,
			metrics.elidedText(spec, Qt::ElideRight, qMax(0, specAvail)));
	}

private:
	QLabel* specSource;
	QLabel* coordinateSource;
};

// Painted chrome layers for the main toolbar (the board's header strip).
// QSS cannot draw the 24px column grid or the status lamp, so the matrix
// toolbar hook parents two transparent, mouse-transparent widgets to the
// toolbar: UnderCells (lowered below every cell) paints the column grid,
// the doubled header rule and the sunken fill of the status readout cell;
// OverCells (raised above the cells) paints the DirtyStatusBadge lamp on
// top of that readout. Instances are found again by object name on every
// hook run (this file has no moc, so findChild by class is unavailable),
// and painting self-suspends while another skin is active because the real
// MainWindow toolbar keeps its children across runtime skin switches.
class MatrixToolbarBoard : public QWidget
{
public:
	enum Layer { UnderCells, OverCells };

	MatrixToolbarBoard(QToolBar* toolBar, Layer boardLayer)
		: QWidget(toolBar), layer(boardLayer)
	{
		setObjectName(layer == UnderCells
			? QStringLiteral("MatrixToolbarBoardUnder")
			: QStringLiteral("MatrixToolbarBoardOver"));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		toolBar->installEventFilter(this);
		if (layer == OverCells)
		{
			// The lamp must follow the badge's dirty-state restyles and the
			// layout moving the cell around.
			if (QWidget* badge = toolBar->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge")))
				badge->installEventFilter(this);
		}
		setGeometry(toolBar->rect());
		if (layer == UnderCells)
			lower();
		else
			raise();
		show();
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
		gridAlpha = skinColorIsDark(QColor(tokens.surface)) ? 55 : 90;
		update();
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget())
		{
			if (event->type() == QEvent::Resize)
				setGeometry(parentWidget()->rect());
		}
		else if (event->type() == QEvent::Paint || event->type() == QEvent::Move
			|| event->type() == QEvent::Resize || event->type() == QEvent::Show
			|| event->type() == QEvent::Hide)
		{
			update();
		}
		return QWidget::eventFilter(watched, event);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		// The layers stay parented to the shared toolbar after a runtime
		// skin switch; they must never leak matrix chrome into another skin.
		if (SkinManager::instance()->currentSkinId() != QStringLiteral("matrix"))
			return;

		QPainter painter(this);
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
		QWidget* badge = parentWidget()->findChild<QWidget*>(QStringLiteral("DirtyStatusBadge"));
		if (badge == nullptr || !badge->isVisible() || !badge->styleSheet().isEmpty())
			return nullptr;
		return badge;
	}

	void paintBoard(QPainter& painter)
	{
		// The faint 24px column grid: the graph paper the whole board sits
		// on, same pitch and ink as the card grid texture.
		QColor grid(ruleColor);
		grid.setAlpha(gridAlpha);
		painter.setPen(QPen(grid, 1));
		for (int x = MatrixMetrics::gridPitch; x < width(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, 0, x, height());

		// Doubled header rule above the QSS bottom border: the strip closes
		// like a board's title rule, not a plain window edge.
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
		// Solid square lamp in traffic-light semantics: green = saved,
		// amber = modified. This is exactly what colour rationing reserves
		// colour for - the one truthful lamp on the header strip.
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

class MatrixSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("matrix"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static CrosspointMatrixRoutingRenderer renderer;
		return &renderer;
	}
	// The "add filter" picker as a two-axis selection instrument: a bus rail
	// of categories and a column of coordinate-labelled entry cells, engaged
	// like a crosspoint (MatrixFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new MatrixFilterPickerView(parent);
	}
	// Reference rows (Include / Convolution / MultiConvolution / VST) as
	// board feed lines: a mono marker cell designates the feed and turns
	// danger while the reference is broken, measured facts sit in boxed
	// sunken mono cells, and the VST body carries the "> IN ... EXTERNAL
	// DEVICE ... OUT >" port strip (MatrixReferenceCardView.cpp).
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new MatrixReferenceCardView(kind, parent);
	}
	// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).

	// Rotary encoder with an LED ring: the value reads as discrete lit
	// segments, the exact value as text in a boxed mono cell. Bipolar knobs
	// light segments left or right of a centre gap (12 o'clock detent);
	// unipolar knobs fill clockwise from the minimum.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override
	{
		const QColor borderColor(tokens.border);
		const QColor accentColor(tokens.accent);
		const QColor cutColor(tokens.accent2);
		const QColor mutedColor(tokens.mutedText);

		// Reserve the bottom strip for the boxed numeric cell when the widget
		// supplies an authoritative value text (e.g. the Preamp card). Promoted
		// legacy dials show their value in the adjacent spin box instead.
		QRect knobArea = rect;
		if (!state.valueText.isEmpty())
			knobArea.adjust(0, 0, 0, -(MatrixMetrics::knobCellHeight + 2));

		QRectF inner = QRectF(knobArea).adjusted(5, 5, -5, -5);
		const double side = qMin(inner.width(), inner.height());
		const QRectF ringRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
		const QPointF center = ringRect.center();
		const double outerRadius = side / 2.0;
		const double innerRadius = qMax(outerRadius - 6.0, 1.0);
		const double bodyRadius = qMax(innerRadius - 3.0, 1.0);

		painter.setRenderHint(QPainter::Antialiasing);

		// Segment ring. An even count gives bipolar knobs a natural centre gap
		// at 12 o'clock; unipolar knobs use an odd count so a segment sits at
		// every position including the centre.
		const int segmentCount = state.bipolar ? 14 : 15;
		const int half = segmentCount / 2;
		int litFrom = 0;
		int litCount = 0;
		bool boost = true;
		if (state.bipolar)
		{
			const double deviation = state.ratio - 0.5;
			boost = deviation >= 0.0;
			litCount = qMin(half, qRound(qAbs(deviation) * 2.0 * half));
			litFrom = boost ? half : half - litCount;
		}
		else
		{
			litCount = qBound(0, qRound(state.ratio * segmentCount), segmentCount);
		}

		QColor litColor = state.bipolar && !boost ? cutColor : accentColor;
		// Lit-segment luminance is calibrated per mode (M1): on the dark board
		// the LEDs gain headroom toward white so a lit cell clearly outshines
		// the ghost ring; the light tokens were derived for maximum contrast
		// on white, where lightening would only desaturate them.
		if (skinColorIsDark(QColor(tokens.surface)))
			litColor = litColor.lighter(112);
		if (state.dragging)
			litColor = litColor.lighter(125);
		else if (state.hovered)
			litColor = litColor.lighter(112);
		// The unlit ring stays visible at low alpha (M1): the range geometry -
		// and the bipolar centre gap - must read even with nothing lit, the
		// way an unlit LED is still a visible part on the board. Muted ink
		// instead of border ink, which vanished against the light card.
		QColor trackColor(mutedColor);
		trackColor.setAlpha(state.enabled ? 80 : 40);

		for (int i = 0; i < segmentCount; i++)
		{
			const double fraction = (i + 0.5) / segmentCount;
			const bool lit = state.enabled && i >= litFrom && i < litFrom + litCount;
			// A lit cell is wider than a ghost cell: LEDs bloom, rules do not.
			QPen segmentPen(lit ? litColor : trackColor, lit ? 3.5 : 2.5, Qt::SolidLine, Qt::FlatCap);
			painter.setPen(segmentPen);
			painter.drawLine(matrixRadialPoint(center, innerRadius, fraction),
				matrixRadialPoint(center, outerRadius, fraction));
		}

		// Centre detent tick: marks the 0-position gap of bipolar knobs so the
		// two knob kinds read differently even at rest. Full text ink (M1):
		// at 0 dB the gap plus this tick is the whole detent statement.
		if (state.bipolar)
		{
			painter.setPen(QPen(state.enabled ? QColor(tokens.text) : QColor(trackColor), 1.0, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(matrixRadialPoint(center, outerRadius + 1.0, 0.5),
				matrixRadialPoint(center, outerRadius + 4.0, 0.5));
		}

		// Encoder body and pointer.
		QColor bodyColor(state.enabled ? tokens.card : tokens.surface);
		painter.setPen(QPen(borderColor, 1.0, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(bodyColor);
		painter.drawEllipse(center, bodyRadius, bodyRadius);
		painter.setPen(QPen(state.enabled ? litColor : QColor(mutedColor), 2.0, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(matrixRadialPoint(center, bodyRadius * 0.45, state.ratio),
			matrixRadialPoint(center, bodyRadius - 1.5, state.ratio));

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Keyboard focus: a square cell bracket, not a glow - this skin's
		// corner language is the rectangle.
		if (state.focused && state.enabled)
		{
			painter.setPen(QPen(accentColor, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(ringRect.toRect().adjusted(-3, -3, 3, 3));
		}

		// Boxed mono numeric cell: the authoritative reading.
		if (!state.valueText.isEmpty())
		{
			QFont monoFont(tokens.monoFontFamily);
			monoFont.setPointSizeF(7.5);
			monoFont.setBold(true);
			const QFontMetrics metrics(monoFont);
			const int cellWidth = qMin(rect.width(), metrics.horizontalAdvance(state.valueText) + 12);
			const QRect cellRect(rect.center().x() - cellWidth / 2,
				rect.bottom() - MatrixMetrics::knobCellHeight + 1, cellWidth, MatrixMetrics::knobCellHeight - 1);
			painter.setPen(QPen(state.dragging ? accentColor : borderColor, 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
			painter.drawRect(cellRect);
			painter.setFont(monoFont);
			if (!state.enabled)
				painter.setPen(QColor(mutedColor));
			else if (state.dragging || state.hovered)
				painter.setPen(accentColor);
			else
				painter.setPen(QColor(tokens.text));
			painter.drawText(cellRect, Qt::AlignCenter, state.valueText);
		}
	}

	// Departure-board cell: square corners, 1px rule, and a 3px status rail in
	// traffic-light semantics (green = active, amber = bypassed). A commented
	// out row additionally swaps the outer rule for a dashed one. A remark row
	// (a pure comment) has no signal state at all: amber would claim it is a
	// bypassed command (colour semantics M3), so its rail is quiet border ink
	// and its rule stays solid - a remark is not a cancelled flight.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool remark = info.type == QStringLiteral("comment");
		const QString railColor = remark ? tokens.border : (info.enabled ? tokens.success : tokens.warning);
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
		const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
		const QString borderStyle = (info.enabled || remark) ? QStringLiteral("solid") : QStringLiteral("dashed");
		QString style = QStringLiteral(
			"QFrame#FilterCardRow { background: %1; border: 1px %2 %3; border-left: 3px solid %4; border-radius: 0px; }")
			.arg(backgroundColor, borderStyle, borderColor, railColor);
		// The :hover rule both signals the row crosspoint and makes Qt repaint
		// the frame on enter/leave, which drives the painted column band.
		style += QStringLiteral(
			" QFrame#FilterCardRow:hover { border: 1px %1 %2; border-left: 3px solid %3; }")
			.arg(borderStyle, tokens.accent, railColor);
		return style;
	}

	// The header strip stays transparent: paintCardChrome owns the band fill,
	// the 1px header rule and the faint column grid behind it.
	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		Q_UNUSED(info);
		Q_UNUSED(tokens);
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0px; }");
	}

	// Monochrome type cell: the command type reads from the mono glyph (the
	// pictogram since feedback round 2, the code text before), not from a
	// per-type colour. Traffic-light colours stay reserved for status.
	QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& tokens) const override
	{
		Q_UNUSED(typeColor);
		const QString ink = info.enabled ? tokens.text : tokens.mutedText;
		return QStringLiteral("color:%1; border-color:%2; background-color:transparent;")
			.arg(ink, tokens.border);
	}

	// The pictogram keeps the cell monochrome: board ink awake, muted asleep.
	QColor typeBadgeInk(const CommandRowInfo& info, const QString&, const QString&, const SkinTokens& tokens) const override
	{
		return QColor(info.enabled ? tokens.text : tokens.mutedText);
	}

	// Row chrome shared by every command type: the coordinate cell and the
	// caption strip. The frozen legacy rows keep their stock construction,
	// and the reference bodies (Include / Convolution / MultiConvolution /
	// VST) speak their board grammar through MatrixReferenceCardView instead
	// of being decorated here.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
	{
		if (info.legacyRow)
			return;

		// The picker's coordinate language imported into the row (M2): the
		// plain line number becomes a board coordinate, the type's bus letter
		// ahead of the stable line position ("B3" = a BiQuad entry on line 3,
		// the picker's C1 cell grammar). Spacer rows are blank board lines
		// and carry no coordinate.
		QLabel* coordinateCell = nullptr;
		if (card != nullptr && header != nullptr && info.type != QStringLiteral("spacer"))
		{
			coordinateCell = header->findChild<QLabel*>(QStringLiteral("FilterCardNumber"));
			if (coordinateCell != nullptr)
			{
				bool plainNumber = false;
				const int line = coordinateCell->text().toInt(&plainNumber);
				if (plainNumber)
					coordinateCell->setText(matrixBusLetter(info.type) + QString::number(line));
			}

			// The caption strip docks under the card body and echoes the raw
			// spec next to that coordinate on hover (the picker footer's
			// grammar; see MatrixRowCaption).
			QVBoxLayout* cardLayout = qobject_cast<QVBoxLayout*>(card->layout());
			if (cardLayout != nullptr)
			{
				QLabel* rawSpec = card->findChild<QLabel*>(QStringLiteral("FilterCardRawPreview"));
				cardLayout->addWidget(new MatrixRowCaption(card, rawSpec, coordinateCell));
			}
		}

		// A bare or unmodelled line (TXT, and the programmatic If/EndIf/Eval
		// vocabulary) is a remark posting, and on this board everything
		// posted lives in a cell (the Comment card's doctrine). The shared
		// raw card lays inline styles on its two labels, so the board answer
		// must be inline too: the ">_" scan glyph becomes a sunken mono
		// designation cell (the "#" marker cell's construction) and the raw
		// line a sunken mono line cell - 1px rules, radius 0, verbatim text.
		if (info.type == QStringLiteral("text") && body != nullptr)
		{
			const SkinTokens& tokens = SkinManager::instance()->tokens();
			if (QLabel* glyph = body->findChild<QLabel*>(QStringLiteral("FilterCardRawGlyph")))
			{
				glyph->setStyleSheet(QStringLiteral(
					"QLabel#FilterCardRawGlyph { background:%1; color:%2; border:1px solid %3;"
					" border-radius:0; padding:2px 7px; font-family:\"%4\"; font-weight:700; font-size:9pt; }")
					.arg(tokens.surfaceSunken, tokens.mutedText, tokens.border, tokens.monoFontFamily));
			}
			if (QLabel* raw = body->findChild<QLabel*>(QStringLiteral("FilterCardRawText")))
			{
				raw->setStyleSheet(QStringLiteral(
					"QLabel#FilterCardRawText { background:%1; color:%2; border:1px solid %3;"
					" border-radius:0; padding:4px 8px; font-family:\"%4\"; font-size:9pt; }")
					.arg(tokens.surfaceSunken, tokens.text, tokens.border, tokens.monoFontFamily));
			}
		}

		// No per-type body decoration remains: the reference bodies build
		// their own board grammar in MatrixReferenceCardView (which also
		// owns the VST port strip that used to be injected from here).
		Q_UNUSED(body);
	}

	// Painted board chrome: header band, 1px header rule, faint column grid,
	// status lamp, and the crosspoint hover (row band + coordinate-column
	// band). Drawn under the transparent header/body so children stay crisp.
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		if (info.type == QStringLiteral("spacer"))
			return;

		painter.setRenderHint(QPainter::Antialiasing, false);

		const QRect content = rect.adjusted(MatrixMetrics::railInset, 1, -1, -1);
		if (content.width() <= 0 || content.height() <= 0)
			return;
		const int headerHeight = qMin(tokens.rowHeight, content.height());
		const QRect headerBand(content.left(), content.top(), content.width(), headerHeight);

		// Header band fill (the header widget itself is transparent).
		painter.fillRect(headerBand, QColor(info.selected ? tokens.surfaceRaised : tokens.cardHover));

		// A remark row (pure comment) is addressable but carries no signal
		// state: full grid ink and hover pre-light like an enabled row, but no
		// status lamp at all (neither green "running" nor amber "bypassed" is
		// true of a note).
		const bool remark = info.type == QStringLiteral("comment");

		// Faint column grid: the graph paper the board sits on. Clipped to the
		// header band - maintainer review (issue #93) judged the texture a
		// distracting afterimage behind parameter widgets, so the row body
		// stays a calm opaque panel regardless of editor widget opacity.
		QColor gridColor(tokens.border);
		const int gridAlpha = skinColorIsDark(QColor(tokens.surface)) ? 80 : 90;
		gridColor.setAlpha((info.enabled || remark) ? gridAlpha : gridAlpha / 2);
		painter.setPen(QPen(gridColor, 1));
		for (int x = content.left() + MatrixMetrics::gridPitch; x < content.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, headerBand.top(), x, headerBand.bottom());

		// 1px rule between the header cell and the body cell.
		if (content.height() > headerHeight)
		{
			painter.setPen(QPen(QColor(tokens.border), 1));
			painter.drawLine(content.left(), content.top() + headerHeight, content.right(), content.top() + headerHeight);
		}

		// Crosspoint hover: the row band and the coordinate-column band light
		// up; their intersection is the crosspoint.
		if (info.hovered && (info.enabled || remark))
		{
			QColor rowBand(tokens.accent);
			rowBand.setAlpha(22);
			painter.fillRect(headerBand, rowBand);
			const QRect columnBand(content.left(), content.top(),
				qMin(MatrixMetrics::coordinateBandWidth, content.width()), content.height());
			QColor columnColor(tokens.accent);
			columnColor.setAlpha(14);
			painter.fillRect(columnBand, columnColor);
			QColor crosspoint(tokens.accent);
			crosspoint.setAlpha(26);
			painter.fillRect(QRect(columnBand.left(), headerBand.top(), columnBand.width(), headerBand.height()), crosspoint);
		}

		// Status lamp in the left gutter: solid green = active, hollow amber =
		// bypassed (traffic-light semantics, never decorative). A remark has no
		// signal state, so it gets no lamp.
		if (!remark)
		{
			const QRect lampRect(content.left() + 1, content.top() + headerHeight / 2 - 3, 5, 5);
			if (info.enabled)
			{
				painter.fillRect(lampRect, QColor(tokens.success));
			}
			else
			{
				painter.setPen(QPen(QColor(tokens.warning), 1));
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(lampRect.adjusted(0, 0, -1, -1));
			}
		}
	}

	// The trailing add row is a vacant board cell: a slot the board has not
	// posted yet (shared insertion contract, docs/skins/README.md). The dashed
	// 1px rule says "no live signal here" - the cancelled-departure dash,
	// form not colour - around the board's own graph paper, and the slot's
	// designation cell holds "+" instead of a bus letter because the letter
	// is only assigned when the picker posts an entry. The caption is a mono
	// board caption. Hover is the crosspoint pre-light (row band + coordinate
	// column band, the card chrome's alphas) with the designation lighting
	// accent like a hovered coordinate readout; pressing engages the slot:
	// solid accent rule + LED-filled designation cell. Crisp throughout.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing, false);

		const QRect cell = rect.adjusted(0, 0, -1, -1);
		if (cell.width() <= 0 || cell.height() <= 0)
			return;

		// The vacant slot exposes the board surface's graph paper (the same
		// faint 24px column grid the masthead and toolbar sit on).
		QColor grid(tokens.border);
		grid.setAlpha(skinColorIsDark(QColor(tokens.surface)) ? 55 : 90);
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

		// Designation cell awaiting its bus letter: a sunken coordinate cell
		// holding "+". Muted at rest like every resting coordinate, accent
		// pre-light on hover, LED fill while engaged.
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

		// Keyboard focus: a square cell bracket around the designation cell
		// (glow-free - this skin's corner language is the rectangle).
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

	// The first-boundary seam: a 1px accent rule with a square insertion
	// cell at its head - the crosspoint about to be patched ahead of line 1.
	// No disc (this skin's corner language is the rectangle), no glow, AA
	// off. The hosting widget paints nothing at rest, so the board stays
	// clean until the cursor crosses the boundary.
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
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
		// LED fill while pressed (the engage grammar of every cell).
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

	// The GraphicEQ response plot as a signal trace on the board: a sunken
	// data ground under a crisp 1px grid, the response as an accent patch
	// trace (routed signal data is the one place accent is legal at rest -
	// the Copy patch-trace / coefficient-cell precedent), nodes as square
	// crosspoint cells (rest = empty 1px-ruled cell, hover = crosspoint
	// pre-light with row/column hairlines through the plot, selected =
	// engaged LED fill), the 0 dB bus as a body-ink rule one rank above the
	// grid, and the cursor probe in a boxed sunken mono cell (rule 5). In
	// the band-locked layouts stems rise off the 0 dB bus in the knob ring's
	// bipolar grammar (boost = accent, cut = accent2) - never danger; a
	// negative gain is not a hazard (colour rationing, article 1). AA stays
	// off for every straight line; only the curve - data, not chrome - is
	// antialiased. Disabled is the cancelled departure: low-alpha content
	// behind a dashed outer rule, never hidden.
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override
	{
		const QColor ground(tokens.graph);
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const QColor cutInk(tokens.accent2);
		const QRect plot = state.plotRect.toRect();

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Cancelled-departure treatment: the instrument stays posted at low
		// alpha; the dashed outer rule below is drawn at full strength.
		if (!state.enabled)
			painter.setOpacity(0.45);

		painter.fillRect(state.rect, ground);

		QFont labelFont(tokens.monoFontFamily);
		labelFont.setPointSizeF(7.5);
		painter.setFont(labelFont);
		const QFontMetrics labelMetrics(labelFont);

		// Crisp 1px grid. The tokens' minor ink is the graph mesh; the major
		// rank is derived from muted ink at low alpha because the shared
		// major token equals the border ink, which the light board cannot
		// tell from the minor mesh. Labels speak DM Mono in muted ink,
		// minors one step quieter.
		QColor majorInk(mutedInk);
		majorInk.setAlpha(90);
		const QColor minorInk(tokens.graphGridMinor);
		QColor minorLabelInk(mutedInk);
		minorLabelInk.setAlpha(150);
		for (const GraphicEQPlotState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(x, plot.top(), x, plot.bottom());
			if (!line.label.isEmpty())
			{
				painter.setPen(line.major ? mutedInk : minorLabelInk);
				painter.drawText(QRect(x - 24, plot.bottom() + 2, 48, state.rect.bottom() - plot.bottom() - 2),
					Qt::AlignHCenter | Qt::AlignTop, line.label);
			}
		}
		for (const GraphicEQPlotState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(plot.left(), y, plot.right(), y);
			if (!line.label.isEmpty())
			{
				painter.setPen(line.major ? mutedInk : minorLabelInk);
				painter.drawText(QRect(state.rect.left(), y - 8, plot.left() - state.rect.left() - 4, 16),
					Qt::AlignRight | Qt::AlignVCenter, line.label);
			}
		}

		// The 0 dB bus: a body-ink 1px rule, one rank of authority above the
		// grid (the trace departs from and returns to this line).
		const bool zeroVisible = state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom();
		if (zeroVisible)
		{
			QColor zeroInk(textInk);
			zeroInk.setAlpha(180);
			painter.setPen(QPen(zeroInk, 1));
			painter.drawLine(plot.left(), int(state.zeroY), plot.right(), int(state.zeroY));
		}

		// Band-locked layouts: level stems off the 0 dB bus, in the LED
		// ring's bipolar grammar - boost lights accent, cut lights accent2.
		const double stemBase = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		if (state.bandLocked)
		{
			for (const QPointF& node : state.nodePositions)
			{
				if (qAbs(node.y() - stemBase) < 1.0)
					continue;
				QColor stem(node.y() < stemBase ? accent : cutInk);
				stem.setAlpha(110);
				painter.setPen(QPen(stem, 2, Qt::SolidLine, Qt::FlatCap));
				painter.drawLine(QPointF(node.x(), stemBase), node);
			}
		}

		// The response trace: 2px accent, antialiased (the curve is data).
		// The fill stays ascetic - a bare wash in the variable layout only,
		// where no stems carry the level reading.
		if (state.curve.size() >= 2)
		{
			painter.setRenderHint(QPainter::Antialiasing, true);
			if (!state.bandLocked)
			{
				QPolygonF wash = state.curve;
				wash.append(QPointF(state.curve.last().x(), stemBase));
				wash.prepend(QPointF(state.curve.first().x(), stemBase));
				QColor washColor(accent);
				washColor.setAlpha(14);
				painter.setPen(Qt::NoPen);
				painter.setBrush(washColor);
				painter.drawPolygon(wash);
			}
			painter.setPen(QPen(accent, 2));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(state.curve);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}

		// Crosspoint pre-light under the hovered node: a row and a column
		// hairline through the plot whose intersection is the node - hover
		// reads as an addressed crosspoint, not a point.
		if (state.enabled && state.hoveredNode >= 0 && state.hoveredNode < state.nodePositions.size())
		{
			const QPointF& hoverNode = state.nodePositions.at(state.hoveredNode);
			QColor hairline(accent);
			hairline.setAlpha(80);
			painter.setPen(QPen(hairline, 1));
			painter.drawLine(int(hoverNode.x()), plot.top(), int(hoverNode.x()), plot.bottom());
			painter.drawLine(plot.left(), int(hoverNode.y()), plot.right(), int(hoverNode.y()));
		}

		// Node cells: square crosspoints. Rest = an empty cell (opaque
		// ground punch + 1px muted rule, the resting-coordinate ink),
		// hover = accent rule + pre-light wash, selected = engaged (LED
		// fill + accent rule). The state ladder rest < hover < engaged.
		for (int i = 0; i < state.nodePositions.size(); i++)
		{
			const QPointF& center = state.nodePositions.at(i);
			const QRect cell(qRound(center.x()) - 3, qRound(center.y()) - 3, 7, 7);
			const bool selected = state.selectedNodes.contains(i);
			const bool nodeHovered = state.hoveredNode == i;
			if (selected)
			{
				painter.setPen(QPen(accent, 1));
				painter.setBrush(accent);
			}
			else if (nodeHovered)
			{
				QColor wash(accent);
				wash.setAlpha(48);
				painter.setPen(QPen(accent, 1));
				painter.setBrush(wash);
			}
			else
			{
				painter.setPen(QPen(mutedInk, 1));
				painter.setBrush(ground);
			}
			painter.drawRect(cell.adjusted(0, 0, -1, -1));
		}

		// The band the readout strip is addressing wears its coordinate tag
		// (mono, muted at rest, accent while engaged - the board coordinate
		// ink), and keyboard focus brackets its cell square (the knob focus
		// grammar; the corner language is the rectangle).
		if (state.focusedNode >= 0 && state.focusedNode < state.nodePositions.size())
		{
			const QPointF& focusNode = state.nodePositions.at(state.focusedNode);
			const QRect cell(qRound(focusNode.x()) - 3, qRound(focusNode.y()) - 3, 7, 7);
			const bool engaged = state.selectedNodes.contains(state.focusedNode);

			QFont tagFont(tokens.monoFontFamily);
			tagFont.setPointSizeF(7.0);
			tagFont.setBold(true);
			const QFontMetrics tagMetrics(tagFont);
			const QString tag = QString::number(state.focusedNode + 1);
			const int tagWidth = tagMetrics.horizontalAdvance(tag);
			int tagX = cell.right() + 5;
			if (tagX + tagWidth > plot.right() - 2)
				tagX = cell.left() - 5 - tagWidth;
			int tagY = cell.top() - 4;
			if (tagY - tagMetrics.ascent() < plot.top() + 2)
				tagY = cell.bottom() + 5 + tagMetrics.ascent();
			painter.setFont(tagFont);
			painter.setPen(engaged ? accent : mutedInk);
			painter.drawText(QPoint(tagX, tagY), tag);
			painter.setFont(labelFont);

			if (state.focused && state.enabled)
			{
				painter.setPen(QPen(accent, 1));
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(cell.adjusted(-3, -3, 2, 2));
			}
		}

		// Cursor probe: the authoritative reading lives in a boxed sunken
		// mono cell (rule 5), posted in the plot's top-right corner.
		if (state.cursorValid && !state.cursorText.isEmpty())
		{
			QFont probeFont(tokens.monoFontFamily);
			probeFont.setPointSizeF(7.5);
			probeFont.setBold(true);
			const QFontMetrics probeMetrics(probeFont);
			const int cellWidth = probeMetrics.horizontalAdvance(state.cursorText) + 12;
			const QRect probeRect(plot.right() - 6 - cellWidth, plot.top() + 6, cellWidth, MatrixMetrics::knobCellHeight);
			painter.setPen(QPen(borderInk, 1));
			painter.setBrush(QColor(tokens.surfaceSunken));
			painter.drawRect(probeRect.adjusted(0, 0, -1, -1));
			painter.setFont(probeFont);
			painter.setPen(textInk);
			painter.drawText(probeRect, Qt::AlignCenter, state.cursorText);
			painter.setFont(labelFont);
		}

		// Outer rule: the data cell's 1px rule; keyboard focus engages it in
		// accent, a bypassed row cancels it with the departure dash at full
		// ink (the row frame's grammar - the dash itself is never dimmed).
		painter.setOpacity(1.0);
		painter.setPen(QPen(state.enabled ? QColor(state.focused ? accent : borderInk) : borderInk, 1,
			state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
	}

	// The board's masthead: the faint 24px column grid behind the title
	// readout and a doubled bottom rule (this inner line plus the QSS bottom
	// border), so the strip closes like a board's title rule, not a plain
	// window edge - the same construction MatrixToolbarBoard gives the
	// toolbar strip. The caption cells stay transparent in QSS so the grid
	// runs through them, exactly like the toolbar's function cells.
	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing, false);

		// The hook carries no mode flag; infer it from the surface lightness
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		QColor grid(tokens.border);
		grid.setAlpha(skinColorIsDark(QColor(tokens.surface)) ? 55 : 90);
		painter.setPen(QPen(grid, 1));
		for (int x = rect.left() + MatrixMetrics::gridPitch; x < rect.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, rect.top(), x, rect.bottom());

		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(rect.left(), rect.bottom() - 3, rect.right(), rect.bottom() - 3);
	}

	// The board's header strip. The neutral stroke icons stay, tinted in
	// plain ink (the catalog is monochrome - colour belongs to the status
	// lamp); the QSS dresses every toolbar item as a square 1px cell, and
	// two painted layers add what QSS cannot express: the 24px column grid
	// behind the cells and the solid square status lamp inside the
	// DirtyStatusBadge readout. Runs at startup and on every skin/dark
	// switch, so the layers are looked up again and re-tinted, never
	// created twice.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override
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
			return board;
		};
		boardLayer(QStringLiteral("MatrixToolbarBoardUnder"), MatrixToolbarBoard::UnderCells)->setBoardTokens(tokens);
		boardLayer(QStringLiteral("MatrixToolbarBoardOver"), MatrixToolbarBoard::OverCells)->setBoardTokens(tokens);
	}
};
}

ISkin* matrixSkin()
{
	static MatrixSkin instance;
	return &instance;
}
