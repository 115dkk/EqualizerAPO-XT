/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

// Constitution: docs/skins/matrix.md
// The file-scope instance is exposed through matrixSkin() so Skins::all()
// can assemble the roster without a central definition list.

#include "Skins.h"

#include <QEvent>
#include <QFileDialog>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QRegion>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/cards/MatrixReferenceCardView.h"
#include "Editor/skins/cards/MatrixSubwooferRoutingCardView.h"
#include "Editor/skins/pickers/MatrixFilterPicker.h"
#include "Editor/widgets/routing/CrosspointMatrixRoutingRenderer.h"
#include "SkinFileIcons.h"
#include "SkinChromeOverlay.h"
#include "SkinPaint.h"
#include "SkinSupport.h"

namespace
{
// ── Matrix (signal-routing matrix / departure board) ────────────────────────
// Constitution: docs/skins/matrix.md

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

// File-dialog pictograms in Matrix's language: chamfered phosphor outlines
// with a faint fill, the shapes an instrument panel would draw on its CRT
// (round-2 verdict: "계기판이나 사이버펑크 화면 키면 나올 법하게"). The one
// bright solid per glyph is a status cell, and audio shows a bar meter -
// data, not decoration. Traffic colours stay reserved for status law, so
// everything here rides the phosphor text ink.
class MatrixFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			QColor faint(ink);
			faint.setAlpha(26);
			painter.setPen(QPen(ink, qMax(1.0, s * 0.065), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
			painter.setBrush(faint);

			// Chamfer: one cut corner, the HUD's way of saying "panel".
			const auto chamferedRect = [&](qreal x, qreal y, qreal w, qreal h) {
				const qreal cut = qMin(w, h) * 0.28;
				QPainterPath path;
				path.moveTo(x, y);
				path.lineTo(x + w - cut, y);
				path.lineTo(x + w, y + cut);
				path.lineTo(x + w, y + h);
				path.lineTo(x, y + h);
				path.closeSubpath();
				painter.drawPath(path);
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				QPainterPath path;
				path.moveTo(s * 0.12, s * 0.78);
				path.lineTo(s * 0.12, s * 0.26);
				path.lineTo(s * 0.42, s * 0.26);
				path.lineTo(s * 0.48, s * 0.36);
				path.lineTo(s * 0.88, s * 0.36);
				path.lineTo(s * 0.88, s * 0.66);
				path.lineTo(s * 0.80, s * 0.78);
				path.closeSubpath();
				painter.drawPath(path);
				painter.fillRect(QRectF(s * 0.18, s * 0.44, s * 0.10, s * 0.07), ink);
				break;
			}
			case Glyph::ConfigFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.drawLine(QPointF(s * 0.33, s * 0.46), QPointF(s * 0.67, s * 0.46));
				painter.drawLine(QPointF(s * 0.33, s * 0.58), QPointF(s * 0.67, s * 0.58));
				painter.drawLine(QPointF(s * 0.33, s * 0.70), QPointF(s * 0.55, s * 0.70));
				break;
			case Glyph::AudioFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.fillRect(QRectF(s * 0.33, s * 0.58, s * 0.08, s * 0.16), ink);
				painter.fillRect(QRectF(s * 0.45, s * 0.44, s * 0.08, s * 0.30), ink);
				painter.fillRect(QRectF(s * 0.57, s * 0.64, s * 0.08, s * 0.10), ink);
				break;
			case Glyph::PluginFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.20));
				painter.drawLine(QPointF(s * 0.44, s * 0.48), QPointF(s * 0.44, s * 0.40));
				painter.drawLine(QPointF(s * 0.56, s * 0.48), QPointF(s * 0.56, s * 0.40));
				break;
			case Glyph::GenericFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				break;
			case Glyph::Drive:
				chamferedRect(s * 0.12, s * 0.30, s * 0.76, s * 0.40);
				painter.drawLine(QPointF(s * 0.20, s * 0.58), QPointF(s * 0.50, s * 0.58));
				painter.fillRect(QRectF(s * 0.70, s * 0.52, s * 0.10, s * 0.10), ink);
				break;
			case Glyph::Computer:
				chamferedRect(s * 0.14, s * 0.18, s * 0.72, s * 0.46);
				painter.fillRect(QRectF(s * 0.22, s * 0.28, s * 0.24, s * 0.06), ink);
				painter.drawLine(QPointF(s * 0.50, s * 0.64), QPointF(s * 0.50, s * 0.76));
				painter.drawLine(QPointF(s * 0.34, s * 0.80), QPointF(s * 0.66, s * 0.80));
				break;
			}
		});
	}
};

namespace
{
// Point on the 270-degree value arc; fraction 0 is bottom-left (7:30), 0.5 is
// 12 o'clock, 1 is bottom-right (4:30). Same sweep as the shared default
// knob; the trig itself lives in SkinPaint.h.
QPointF matrixRadialPoint(const QPointF& center, double radius, double fraction)
{
	return skinArcPoint(center, radius, -(135.0 + 270.0 * fraction));
}

// Bus letter of a command type for the row coordinate ("B3"). Letters are
// designations, not identifiers - two types may share one; uniqueness stays
// with the line number.
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
	// If/Eval get their own designations so they stay out of the R (remark)
	// carrier.
	if (type == QStringLiteral("if"))
		return QStringLiteral("F");
	if (type == QStringLiteral("eval"))
		return QStringLiteral("E");
	if (type == QStringLiteral("stage"))
		return QStringLiteral("S");
	if (type == QStringLiteral("loudness"))
		return QStringLiteral("L");
	if (type == QStringLiteral("comment"))
		return QStringLiteral("#");
	// Unrecognized raw lines: a remark entry on the board.
	return QStringLiteral("R");
}

// Per-row caption strip: a sunken board line under the card body echoing the
// row's raw spec next to its coordinate, lighting on row hover. The strip
// needs no event machinery: the frame's :hover QSS rule already forces a
// frame repaint on enter/leave (the same trigger the painted column band
// uses), which redraws this child, and the gallery's WA_UnderMouse hover
// equivalent drives it the same way. It replaces the shared raw-preview
// strip for this skin (tokens().showRawPreview = false).
class MatrixRowCaption : public QWidget
{
public:
	MatrixRowCaption(QWidget* card, QLabel* specSource, QLabel* coordinateSource)
		: QWidget(card), specSource(specSource), coordinateSource(coordinateSource)
	{
		setObjectName(QStringLiteral("MatrixRowCaption"));
		configurePaintOnlyChrome(this);
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
		// A line swallowed by a false If branch idles at the cancelled ink
		// depth but keeps its verbatim spec: no "#" appears in the raw line
		// and no cancel treatment is added here.
		const bool skipped = card != nullptr && card->property("lineSkipped").toBool();
		const bool lit = enabled && card != nullptr && card->underMouse();

		painter.fillRect(rect(), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(0, 0, width() - 1, 0);

		QFont mono(tokens.monoFontFamily);
		mono.setPointSizeF(7.5);
		painter.setFont(mono);
		const QFontMetrics metrics(mono);

		QColor idleInk(tokens.mutedText);
		if (!enabled || skipped)
			idleInk.setAlpha(120);
		const QColor accent(tokens.accent);
		const int pad = 10;

		const QString coordinate = coordinateSource != nullptr ? coordinateSource->text() : QString();
		const int coordinateWidth = metrics.horizontalAdvance(coordinate);
		painter.setPen(lit ? accent : idleInk);
		painter.drawText(QRect(width() - pad - coordinateWidth, 0, coordinateWidth, height()),
			Qt::AlignVCenter | Qt::AlignLeft, coordinate);

		// The raw-preview label keeps its text current on every model rebuild
		// even while hidden, so it doubles as the live source of the raw spec.
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

class MatrixSkin : public ISkin
{
public:
	QString id() const override { return QStringLiteral("matrix"); }
	IRoutingRenderer* routingRenderer() const override
	{
		static CrosspointMatrixRoutingRenderer renderer;
		return &renderer;
	}
	// Two-axis picker: a bus rail of categories and a column of
	// coordinate-labelled entry cells (MatrixFilterPicker.cpp).
	FilterPickerView* createFilterPicker(QWidget* parent) const override
	{
		return new MatrixFilterPickerView(parent);
	}
	// Reference rows (Include / Convolution / MultiConvolution / VST) as
	// board feed lines (MatrixReferenceCardView.cpp).
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override
	{
		return new MatrixReferenceCardView(kind, parent);
	}

	SubwooferRoutingCardView* createSubwooferRoutingCardView(QWidget* parent) const override
	{
		return new MatrixSubwooferRoutingCardView(parent);
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
		// Lit-segment luminance is calibrated per mode: on the dark board
		// the LEDs gain headroom toward white so a lit cell clearly outshines
		// the ghost ring; the light tokens were derived for maximum contrast
		// on white, where lightening would only desaturate them.
		if (tokens.dark)
			litColor = litColor.lighter(112);
		if (state.dragging)
			litColor = litColor.lighter(125);
		else if (state.hovered)
			litColor = litColor.lighter(112);
		// The unlit ring stays visible at low alpha: the range geometry -
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
		// two knob kinds read differently even at rest. Full text ink: at
		// 0 dB the gap plus this tick is the whole detent statement.
		if (state.bipolar)
		{
			painter.setPen(QPen(state.enabled ? QColor(tokens.text) : QColor(trackColor), 1.0, Qt::SolidLine, Qt::FlatCap));
			painter.drawLine(matrixRadialPoint(center, outerRadius + 1.0, 0.5),
				matrixRadialPoint(center, outerRadius + 4.0, 0.5));
		}

		QColor bodyColor(state.enabled ? tokens.card : tokens.surface);
		painter.setPen(QPen(borderColor, 1.0, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(bodyColor);
		painter.drawEllipse(center, bodyRadius, bodyRadius);
		painter.setPen(QPen(state.enabled ? litColor : QColor(mutedColor), 2.0, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(matrixRadialPoint(center, bodyRadius * 0.45, state.ratio),
			matrixRadialPoint(center, bodyRadius - 1.5, state.ratio));

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Keyboard focus: a square cell bracket, not a glow.
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

	// Departure-board cell: square corners, 1px rule, 3px status rail. A
	// remark row (pure comment) gets a quiet border-ink rail and a solid
	// rule; a line a false If branch swallowed (lineSkipped) keeps a quiet
	// border-ink rail behind the cancellation dash, and the header inks dim
	// via the QSS lineSkipped key. This hook re-runs from every repaint
	// (refreshStateProperties), so the advisory analysis facts are read at
	// paint time as required.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool remark = info.type == QStringLiteral("comment");
		const bool cancelled = !remark && info.enabled && info.lineSkipped;
		const QString railColor = remark || cancelled ? tokens.border : (info.enabled ? tokens.success : tokens.warning);
		const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
		const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
		const QString borderStyle = ((info.enabled && !cancelled) || remark) ? QStringLiteral("solid") : QStringLiteral("dashed");
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

	// Monochrome type cell: typeColor is deliberately ignored - the command
	// type reads from the mono glyph, never from a per-type colour.
	BadgeTreatment badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const override
	{
		Q_UNUSED(typeColor);
		Q_UNUSED(badgeToken);
		const QString ink = info.enabled ? tokens.text : tokens.mutedText;
		return {
			QStringLiteral("color:%1; border-color:%2; background-color:transparent;")
				.arg(ink, tokens.border),
			QColor(ink)
		};
	}

	// Row chrome shared by every command type: the coordinate cell and the
	// caption strip. The frozen legacy rows keep their stock construction,
	// and the reference bodies (Include / Convolution / MultiConvolution /
	// VST) speak their board grammar through MatrixReferenceCardView instead
	// of being decorated here.
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const override
	{
		if (info.legacyRow)
			return;

		// The plain line number becomes a board coordinate: the type's bus
		// letter ahead of the stable line position ("B3"). Spacer rows are
		// blank board lines and carry no coordinate.
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
			// spec next to that coordinate on hover (see MatrixRowCaption).
			QVBoxLayout* cardLayout = qobject_cast<QVBoxLayout*>(card->layout());
			if (cardLayout != nullptr)
			{
				QLabel* rawSpec = card->findChild<QLabel*>(QStringLiteral("FilterCardRawPreview"));
				cardLayout->addWidget(new MatrixRowCaption(card, rawSpec, coordinateCell));
			}
		}

		// Bare/unmodelled lines (TXT, the If/EndIf/Eval vocabulary) live in
		// cells. The shared raw card lays inline styles on its two labels,
		// so the board answer must be inline too: the ">_" scan glyph
		// becomes a sunken mono designation cell and the raw line a sunken
		// mono line cell.
		if ((info.type == QStringLiteral("text") || info.type == QStringLiteral("if")
			|| info.type == QStringLiteral("eval") || info.dynamicLine) && body != nullptr)
		{
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

		// The reference bodies build their own board grammar in
		// MatrixReferenceCardView; no per-type body decoration here.
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
		// state: full grid ink and hover pre-light like an enabled row, but
		// no status lamp.
		const bool remark = info.type == QStringLiteral("comment");

		// Faint column grid, clipped to the header band: the row body stays a
		// calm opaque panel regardless of editor widget opacity (invariant
		// rule 3 of the constitution).
		QColor gridColor(tokens.border);
		const int gridAlpha = tokens.dark ? 80 : 90;
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

		// Status lamp in the left gutter: solid green = active, hollow amber
		// = bypassed; a remark gets no lamp. Gate rows post the engine's
		// branch decision in the same lamp grammar, and a swallowed line
		// un-lights its lamp. The analysis facts are advisory and only read
		// here, at paint time.
		if (!remark)
		{
			const QRect lampRect(content.left() + 1, content.top() + headerHeight / 2 - 3, 5, 5);
			const auto solidLamp = [&](const QColor& ink)
			{
				painter.fillRect(lampRect, ink);
			};
			const auto hollowLamp = [&](const QColor& ink)
			{
				painter.setPen(QPen(ink, 1));
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(lampRect.adjusted(0, 0, -1, -1));
			};
			if (!info.enabled)
			{
				hollowLamp(QColor(tokens.warning));
			}
			else if (info.type == QStringLiteral("if"))
			{
				// Gate lamp: taken = solid success, condition false = hollow
				// success, evaluation fault = solid danger, unreached or not
				// yet analysed = hollow muted (no decision posted).
				if (info.branchState == 1)
					solidLamp(QColor(tokens.success));
				else if (info.branchState == 3)
					solidLamp(QColor(tokens.danger));
				else if (info.branchState == 0)
					hollowLamp(QColor(tokens.success));
				else
					hollowLamp(QColor(tokens.mutedText));
			}
			else if (info.lineSkipped)
			{
				// Cancelled departure: live source behind a closed gate keeps
				// its running lamp, unlit.
				hollowLamp(QColor(tokens.success));
			}
			else
			{
				solidLamp(QColor(tokens.success));
			}
		}

		// Computed-value readout: the engine's Eval result or the substituted
		// inline-expression text in a boxed sunken mono cell, right-aligned
		// in the header. Body ink; a parser fault posts in danger. Paint-time
		// only by design: the facts go stale between an edit and the next
		// analysis run, and this cell repaints with them instead of baking
		// them into a construction-time label.
		if (!info.evalText.isEmpty() || (info.type == QStringLiteral("eval") && info.valueError))
		{
			const QString reading = QStringLiteral("= ")
				+ (info.evalText.isEmpty() ? QStringLiteral("ERR") : info.evalText);
			QFont mono(tokens.monoFontFamily);
			mono.setPointSizeF(7.5);
			mono.setBold(true);
			const QFontMetrics metrics(mono);
			// The button train keeps its reserved zone on the right; the four
			// buttons span ~180px from the band's right
			// edge and the cell is painted *under* them, so the reserve must
			// clear the train entirely or the cell's tail hides beneath the
			// power button. The cell never grows left into the coordinate
			// band, and a reading that still does not fit is elided, never
			// squeezed.
			const int reserved = 192;
			const int cellRight = headerBand.right() - reserved;
			const int maxWidth = qMin(220, cellRight - headerBand.left() - MatrixMetrics::coordinateBandWidth);
			if (maxWidth > 40)
			{
				const QString elided = metrics.elidedText(reading, Qt::ElideRight, maxWidth - 12);
				const int cellWidth = qMin(maxWidth, metrics.horizontalAdvance(elided) + 12);
				const QRect cellRect(cellRight - cellWidth,
					headerBand.top() + (headerBand.height() - MatrixMetrics::knobCellHeight) / 2,
					cellWidth, MatrixMetrics::knobCellHeight);
				painter.setPen(QPen(info.valueError ? QColor(tokens.danger) : QColor(tokens.border), 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(cellRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(mono);
				painter.setPen(info.valueError ? QColor(tokens.danger) : QColor(tokens.text));
				painter.drawText(cellRect, Qt::AlignCenter, elided);
			}
		}
	}

	// The If block as a printed bracket: one crisp 1px muted rule per open
	// scope, opening under the gate's head row and closing on the EndIf row
	// with an L-corner. The bracket is structure only: the engine's
	// decisions post on the cells themselves (gate lamps, cancelled rows,
	// value readouts), never in the gutter.
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override
	{
		const bool ifFamily = info.type == QStringLiteral("if");
		const bool headRow = ifFamily && info.command == QStringLiteral("if");
		const bool branchOrTail = ifFamily && !headRow;
		const bool tailRow = ifFamily && info.command == QStringLiteral("endif");
		const int logic = info.logicDepth;
		if (!headRow && logic <= 0)
			return false;

		painter.setRenderHint(QPainter::Antialiasing, false);

		// Lane geometry from the row widget: the indent unit, how many bands are
		// drawn, where the card face starts, and where a band's centre is. The
		// branch/tail rows' extra unit (logicSiblingsIndentAsMembers) is already
		// folded into laneCount, because the same call sets the row's own margin.
		const int h = size.height();
		const int indentUnits = info.laneCount;
		const int cardLeft = info.cardLeft;
		// Rules sit on the centres of the existing indent bands; the bracket
		// claims no positions of its own.
		const auto bandCenter = [&info](int level) { return info.laneCenter(level); };

		// The innermost logicDepth bands are If lanes; any bands outside them
		// are channel groups and keep a quiet 1px border-ink rule, one ink
		// rank below the bracket, so scope reads above grouping.
		const int ifLevels = headRow ? logic : branchOrTail ? logic - 1 : logic;
		const int channelLevels = qMax(0, indentUnits - ifLevels - (branchOrTail ? 1 : 0));
		painter.setPen(QPen(QColor(tokens.border), 1));
		for (int level = 0; level < channelLevels; level++)
			painter.drawLine(bandCenter(level), 0, bandCenter(level), h);

		painter.setPen(QPen(QColor(tokens.mutedText), 1));
		const int junctionY = 4 + tokens.rowHeight / 2;
		if (headRow)
		{
			for (int level = channelLevels; level < channelLevels + logic; level++)
				painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
			// The bracket opens under the head: its rule first shows in the
			// margin below the gate's full-width cell.
			const int own = bandCenter(channelLevels + logic);
			painter.drawLine(own, h - 4, own, h);
		}
		else if (tailRow)
		{
			for (int level = channelLevels; level + 1 < channelLevels + logic; level++)
				painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
			// The closing L-corner: down to the tail's centre line, then a
			// half-pitch tick to the EndIf cell's edge.
			const int own = bandCenter(channelLevels + logic - 1);
			painter.drawLine(own, 0, own, junctionY);
			painter.drawLine(own, junctionY, cardLeft - 1, junctionY);
		}
		else
		{
			// Members and branch rows: every open bracket passes straight
			// through. ElseIf/Else post their state on their own cells (the
			// gate lamp), not on the bracket.
			for (int level = channelLevels; level < channelLevels + logic; level++)
				painter.drawLine(bandCenter(level), 0, bandCenter(level), h);
		}
		return true;
	}

	// Branch/tail rows (ElseIf/Else/EndIf) mount one indent unit past their
	// semantic level, with the block members, so the bracket lane passes
	// them instead of dying behind their full-width cells.
	bool logicSiblingsIndentAsMembers() const override
	{
		return true;
	}

	// The trailing add row: a vacant board slot behind a dashed 1px rule,
	// with a "+" designation cell awaiting its bus letter (shared insertion
	// contract, docs/skins/README.md).
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override
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

	// The first-boundary seam: a 1px accent rule with a square insertion
	// cell at its head. The hosting widget paints nothing at rest.
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

	// The GraphicEQ response plot as a signal trace on the board (see the
	// constitution's GraphicEQ section). AA stays off for every straight
	// line; only the curve - data, not chrome - is antialiased.
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

		// Disabled: content at low alpha; the dashed outer rule below is
		// drawn at full strength.
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
		// grid.
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
		// hairline through the plot whose intersection is the node.
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
		// (mono, muted at rest, accent while engaged), and keyboard focus
		// brackets its cell square.
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

		// Cursor probe: a boxed sunken mono cell in the plot's top-right
		// corner.
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

		// Outer rule: keyboard focus engages it in accent, a bypassed row
		// cancels it with a dash at full ink (the dash itself is never
		// dimmed).
		painter.setOpacity(1.0);
		painter.setPen(QPen(state.enabled ? QColor(state.focused ? accent : borderInk) : borderInk, 1,
			state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
	}

	// The analysis dock's response graph: the GraphicEQ signal-trace
	// instrument adapted to a wide always-on readout (crisp grid, tag-cell
	// axis figures, zero bus, accent trace over one echo stroke, amber
	// hazard hatching when the response can clip, a scan-rule cursor and a
	// board-line footer).
	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override
	{
		const QColor ground(tokens.graph);
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const bool darkBoard = tokens.dark;
		// Caution ink: full amber only on the dark board. On the light board
		// raw orange reads as crayon against the ice palette, so it sinks to
		// a printed ochre - hue kept, saturation and value derived down.
		const QColor warnBase(tokens.warning);
		const QColor hazardInk = darkBoard ? warnBase
			: QColor::fromHsvF(warnBase.hsvHueF(), warnBase.hsvSaturationF() * 0.82, warnBase.valueF() * 0.62);
		const QRect plot = state.plotRect.toRect();

		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.fillRect(state.rect, ground);

		// Crisp 1px grid, integer-aligned; same rank derivation as the
		// GraphicEQ plot.
		QColor majorInk(mutedInk);
		majorInk.setAlpha(90);
		const QColor minorInk(tokens.graphGridMinor);
		// Unwrapped phase runs past half turns, and on this board a half turn
		// is a landmark: a rule standing on a multiple of 180 degrees takes the
		// major rank even though the axis marks only zero as major, so an
		// all-pass sweep reads as counted turns instead of an even ladder.
		// Magnitude and group delay have no such landmark and keep the ranks
		// they arrive with. The value behind a rule is recovered from its y
		// through the same mapping the widget used to place it.
		const double valueSpan = state.maximum - state.minimum;
		const auto landmarkRule = [&state, valueSpan](const AnalysisGraphState::GridLine& line) {
			if (state.metric != AnalysisMetric::PhaseDegrees || valueSpan <= 0.0
				|| state.plotRect.height() <= 0.0)
				return false;
			const double value = state.maximum
				- (line.pos - state.plotRect.top()) / state.plotRect.height() * valueSpan;
			const double turns = value / 180.0;
			return qAbs(turns - qRound(turns)) < 1e-6;
		};
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(x, plot.top(), x, plot.bottom());
		}
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major || landmarkRule(line) ? majorInk : minorInk, 1));
			painter.drawLine(plot.left(), y, plot.right(), y);
		}

		// Hazard zone: the response can clip, so the whole over-bus band
		// posts thin amber diagonals (AA off - the pixel staircase is
		// deliberate).
		const int zeroYpx = int(state.zeroY);
		if (state.clipping && zeroYpx > plot.top())
		{
			const QRect zone(plot.left(), plot.top(), plot.width(), zeroYpx - plot.top());
			QColor hatch(hazardInk);
			hatch.setAlpha(darkBoard ? 60 : 70);
			QPainterStateGuard hazardState(&painter);
			painter.setClipRect(zone);
			painter.setPen(QPen(hatch, 1));
			// Diagonals on the board's 12px half-pitch (gridPitch 24 = two
			// rows of 12): thin rules that read individually, not a texture.
			for (int x = zone.left() - zone.height(); x <= zone.right(); x += 12)
				painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());

			// Where the trace actually exceeds the bus, the hazard densifies:
			// half-pitch diagonals at full caution ink, clipped to the area
			// between the trace and the 0 dB rule. The band says "this side
			// can clip"; the dense region says WHERE and BY HOW MUCH. One
			// closed cell per segment, cut in a single stencil, so the dense
			// ink lands only where the board actually posted a reading.
			QPainterPath overshoot;
			for (const QPolygonF& segment : state.curves)
			{
				if (segment.size() < 2)
					continue;
				QPolygonF closed = segment;
				closed.append(QPointF(segment.last().x(), state.zeroY));
				closed.append(QPointF(segment.first().x(), state.zeroY));
				overshoot.addPolygon(closed);
				overshoot.closeSubpath();
			}
			if (!overshoot.isEmpty())
			{
				painter.setClipPath(overshoot, Qt::IntersectClip);
				QColor dense(hazardInk);
				dense.setAlpha(darkBoard ? 165 : 190);
				painter.setPen(QPen(dense, 1));
				for (int x = zone.left() - zone.height(); x <= zone.right(); x += 5)
					painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());
			}
		}

		// The zero bus: the board's reference rule, one rank of authority
		// above the grid. Posted only when the metric's zero sits inside the
		// fitted range, and - for the metrics that can land it there - only
		// when it is clear of the frame: a group delay that never goes
		// negative and a phase that never rises above zero both push it onto
		// the outer rule, where the same 1px line is a border and not a bus.
		// Magnitude fits symmetrically, so its bus is always inside the pane
		// and this second test can never fire on it.
		const bool zeroOnFrame = state.metric != AnalysisMetric::MagnitudeDb
			&& (zeroYpx <= plot.top() + 1 || zeroYpx >= plot.bottom() - 1);
		if (state.zeroVisible && !zeroOnFrame)
		{
			QColor zeroInk(textInk);
			zeroInk.setAlpha(180);
			painter.setPen(QPen(zeroInk, 1));
			painter.drawLine(plot.left(), zeroYpx, plot.right(), zeroYpx);
		}

		// Phase and group delay have no value at all inside a null, so the
		// trace arrives in pieces. A departure board does not leave a slot
		// blank: the columns with no reading are bracketed by the cancellation
		// dash (form, never colour - a null is a reading the board could not
		// take, not a fault) and, where the gap is wide enough to carry it,
		// posted with the picker's empty-scan wording in a sunken mono cell.
		// Magnitude always arrives as one segment, so none of this is on its
		// path.
		if (state.metric != AnalysisMetric::MagnitudeDb)
		{
			QFont gapFont(tokens.monoFontFamily);
			gapFont.setPointSizeF(7.0);
			gapFont.setBold(true);
			const QFontMetrics gapMetrics(gapFont);
			const QString gapText = QStringLiteral("NO SIGNAL");
			const int gapCellWidth = gapMetrics.horizontalAdvance(gapText) + 12;
			const int gapCellHeight = gapMetrics.height() + 2;
			const auto postGap = [&](double from, double to) {
				const int left = qRound(from);
				const int right = qRound(to);
				// A one or two column hole is already legible as a break in
				// the trace; bracketing it would print two dashes on top of
				// each other.
				if (right - left < 3)
					return;
				painter.setPen(QPen(borderInk, 1, Qt::DashLine));
				if (left > plot.left())
					painter.drawLine(left, plot.top() + 1, left, plot.bottom() - 1);
				if (right < plot.right())
					painter.drawLine(right, plot.top() + 1, right, plot.bottom() - 1);
				if (right - left < gapCellWidth + 10)
					return;
				const QRect gapRect((left + right - gapCellWidth) / 2,
					plot.center().y() - gapCellHeight / 2, gapCellWidth, gapCellHeight);
				painter.setPen(QPen(borderInk, 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(gapRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(gapFont);
				painter.setPen(mutedInk);
				painter.drawText(gapRect, Qt::AlignCenter, gapText);
			};
			// The complement of what the segments cover, so a hole at either
			// end of the axis is posted the same way as one in the middle.
			double coveredTo = state.plotRect.left();
			for (const QPolygonF& segment : state.curves)
			{
				if (segment.isEmpty())
					continue;
				postGap(coveredTo, segment.first().x());
				coveredTo = qMax(coveredTo, segment.last().x());
			}
			postGap(coveredTo, state.plotRect.right());
		}

		// Mono axis figures in tag cells punched out of the grid (ground fill
		// under the figure). Majors speak muted ink, minors one step quieter.
		// Frequency tags ride the bottom edge; a tag that would collide with
		// its neighbour is skipped, never squeezed.
		QFont tagFont(tokens.monoFontFamily);
		tagFont.setPointSizeF(7.0);
		const QFontMetrics tagMetrics(tagFont);
		const int tagHeight = tagMetrics.height();
		QColor minorLabelInk(mutedInk);
		minorLabelInk.setAlpha(150);
		painter.setFont(tagFont);
		int lastTagRight = state.rect.left() - 100;
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			if (line.label.isEmpty())
				continue;
			const int tagWidth = tagMetrics.horizontalAdvance(line.label) + 6;
			int tagX = int(line.pos) - tagWidth / 2;
			tagX = qBound(state.rect.left() + 1, tagX, state.rect.right() - tagWidth - 1);
			if (tagX <= lastTagRight + 4)
				continue;
			const QRect tagRect(tagX, plot.bottom() - tagHeight - 1, tagWidth, tagHeight);
			painter.fillRect(tagRect, ground);
			painter.setPen(line.major ? mutedInk : minorLabelInk);
			painter.drawText(tagRect, Qt::AlignCenter, line.label);
			lastTagRight = tagRect.right();
		}

		// Value tags ride the left edge. When the fitted range packs the value
		// rules tighter than a figure, thin the tags anchored on the zero bus
		// so the bus always keeps its figure. A tag that cannot centre on its
		// rule inside the plot (the range extremes at the plot edges) is
		// dropped, not squeezed - the footer's span readout posts those two
		// figures, and a tag off its rule would lie about its coordinate.
		int labelStep = 1;
		if (state.horizontal.size() >= 2)
		{
			const double spacing = qAbs(state.horizontal.at(1).pos - state.horizontal.at(0).pos);
			if (spacing > 0.5)
				labelStep = qMax(1, qCeil((tagHeight + 3) / spacing));
		}
		int zeroIndex = 0;
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			if (state.horizontal.at(i).major)
			{
				zeroIndex = i;
				break;
			}
		}
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
			if (line.label.isEmpty() || (i - zeroIndex) % labelStep != 0)
				continue;
			const int tagWidth = tagMetrics.horizontalAdvance(line.label) + 6;
			const int tagY = int(line.pos) - tagHeight / 2;
			if (tagY < plot.top() + 1 || tagY + tagHeight > plot.bottom() - 1)
				continue;
			const QRect tagRect(plot.left() + 4, tagY, tagWidth, tagHeight);
			painter.fillRect(tagRect, ground);
			// The tag keeps the rank of the rule it stands on, so a promoted
			// half-turn landmark is named as loudly as it is drawn.
			painter.setPen(line.major || landmarkRule(line) ? mutedInk : minorLabelInk);
			painter.drawText(tagRect, Qt::AlignCenter, line.label);
		}

		// The response trace: an accent core over a single wider low-alpha
		// echo stroke. The curve is data, so it alone is antialiased. One run
		// of the stroke pair per segment - the trace breaks where the metric
		// has no reading, and a stroke bridging the break would post a figure
		// the board never measured.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;
			painter.setRenderHint(QPainter::Antialiasing, true);
			QColor echo(accent);
			echo.setAlpha(70);
			painter.setPen(QPen(echo, 3));
			painter.drawPolyline(segment);
			painter.setPen(QPen(accent, 1));
			painter.drawPolyline(segment);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}

		// The clip peak wears an OVER tag: a boxed cell in caution amber
		// pinned to the highest point of the trace, tied down by a 1px tick.
		// The scan runs across every segment - one tag for the whole board,
		// on the loudest point wherever it landed.
		if (state.clipping)
		{
			QPointF peak;
			bool peakFound = false;
			for (const QPolygonF& segment : state.curves)
			{
				if (segment.size() < 2)
					continue;
				for (const QPointF& point : segment)
				{
					if (!peakFound || point.y() < peak.y())
					{
						peak = point;
						peakFound = true;
					}
				}
			}
			if (peakFound && peak.y() < state.zeroY)
			{
				QFont overFont(tokens.monoFontFamily);
				overFont.setPointSizeF(7.0);
				overFont.setBold(true);
				const QFontMetrics overMetrics(overFont);
				const QString overText = QStringLiteral("OVER");
				const int overWidth = overMetrics.horizontalAdvance(overText) + 8;
				const int overHeight = overMetrics.height() + 2;
				int overX = qRound(peak.x()) - overWidth / 2;
				overX = qBound(plot.left() + 2, overX, plot.right() - overWidth - 2);
				int overY = qRound(peak.y()) - 5 - overHeight;
				bool above = true;
				if (overY < plot.top() + 2)
				{
					overY = qRound(peak.y()) + 5;
					above = false;
				}
				const QRect overRect(overX, overY, overWidth, overHeight);
				painter.setPen(QPen(hazardInk, 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(overRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(overFont);
				painter.drawText(overRect, Qt::AlignCenter, overText);
				const int tickX = qBound(overRect.left() + 1, qRound(peak.x()), overRect.right() - 1);
				if (above)
					painter.drawLine(tickX, overRect.bottom() + 1, tickX, qRound(peak.y()) - 2);
				else
					painter.drawLine(tickX, overRect.top() - 1, tickX, qRound(peak.y()) + 2);
			}
		}

		// Cursor: the scan rule energizes from dim to full with the widget's
		// hover progress; a square corner-bracket reticle marks where it
		// crosses the trace, and the reading slides with the pointer in a
		// boxed sunken mono cell.
		if (state.cursorValid)
		{
			const int scanX = qBound(plot.left(), qRound(state.cursor.x()), plot.right());
			QColor scanInk(accent);
			scanInk.setAlpha(90 + qRound(state.hover * 165.0));
			painter.setPen(QPen(scanInk, 1));
			painter.drawLine(scanX, plot.top(), scanX, plot.bottom());

			// Target acquired, but only where there is a target: a column the
			// metric has no value for hands over a clamped y, and a reticle
			// pinned to the frame edge would claim a crossing the response
			// never had. The scan rule stays (the board is still addressing
			// that column) and the gap posting above says why nothing is
			// there. Magnitude always has a reading, so it always brackets.
			const bool noReading = state.metric != AnalysisMetric::MagnitudeDb
				&& state.cursorText.isEmpty();
			if (!noReading)
			{
				const int crossY = qRound(state.curveYAtCursor);
				const int bracketLeft = scanX - 5;
				const int bracketRight = scanX + 5;
				const int bracketTop = crossY - 5;
				const int bracketBottom = crossY + 5;
				const int leg = 3;
				QColor reticleInk(accent);
				reticleInk.setAlpha(140 + qRound(state.hover * 115.0));
				painter.setPen(QPen(reticleInk, 1));
				painter.drawLine(bracketLeft, bracketTop, bracketLeft + leg, bracketTop);
				painter.drawLine(bracketLeft, bracketTop, bracketLeft, bracketTop + leg);
				painter.drawLine(bracketRight - leg, bracketTop, bracketRight, bracketTop);
				painter.drawLine(bracketRight, bracketTop, bracketRight, bracketTop + leg);
				painter.drawLine(bracketLeft, bracketBottom - leg, bracketLeft, bracketBottom);
				painter.drawLine(bracketLeft, bracketBottom, bracketLeft + leg, bracketBottom);
				painter.drawLine(bracketRight, bracketBottom - leg, bracketRight, bracketBottom);
				painter.drawLine(bracketRight - leg, bracketBottom, bracketRight, bracketBottom);
			}

			if (!state.cursorText.isEmpty())
			{
				QFont probeFont(tokens.monoFontFamily);
				probeFont.setPointSizeF(7.5);
				probeFont.setBold(true);
				const QFontMetrics probeMetrics(probeFont);
				const int probeWidth = probeMetrics.horizontalAdvance(state.cursorText) + 12;
				int probeX = scanX - probeWidth / 2;
				probeX = qBound(plot.left() + 2, probeX, plot.right() - probeWidth - 2);
				const QRect probeRect(probeX, plot.top() + 4, probeWidth, MatrixMetrics::knobCellHeight);
				painter.setPen(QPen(mixColor(borderInk, accent, state.hover), 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(probeRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(probeFont);
				painter.setPen(textInk);
				painter.drawText(probeRect, Qt::AlignCenter, state.cursorText);
			}
		}

		// Terse board caption in the masthead margin (a painted stylistic
		// caption in the toolbar caption grammar, not user data). A board
		// names what it is carrying, and the metric switch changes exactly
		// that, so the two new quantities take the masthead with the unit in
		// brackets behind them - the unit arrives finished in the state and is
		// only put in board case here. Magnitude keeps the designation it has
		// always had.
		QString masthead = QStringLiteral("RESPONSE");
		if (state.metric != AnalysisMetric::MagnitudeDb)
		{
			const QString quantity = state.metric == AnalysisMetric::PhaseDegrees
				? QStringLiteral("PHASE")
				: QStringLiteral("GROUP DELAY");
			masthead = state.unit.isEmpty()
				? quantity
				: QStringLiteral("%1 [%2]").arg(quantity, state.unit.toUpper());
		}
		QFont captionFont(tokens.monoFontFamily);
		captionFont.setPointSizeF(7.0);
		captionFont.setWeight(QFont::DemiBold);
		captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(captionFont);
		painter.setPen(mutedInk);
		painter.drawText(QRect(plot.left(), state.rect.top() + 2, qMax(0, plot.width()), 12),
			Qt::AlignLeft | Qt::AlignVCenter, masthead);

		// Footer: a sunken board line under a 1px rule. "> " marker, then the
		// prepared channel/sample-rate caption exactly as handed over
		// (localized data, elided when tight), lit from muted to body ink by
		// hover; the fitted span reads on the right.
		const int footerTop = state.rect.bottom() - 17;
		painter.fillRect(QRect(state.rect.left() + 1, footerTop + 1, state.rect.width() - 2, 16), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(borderInk, 1));
		painter.drawLine(state.rect.left() + 1, footerTop, state.rect.right() - 1, footerTop);

		QFont footerFont(tokens.monoFontFamily);
		footerFont.setPointSizeF(7.5);
		painter.setFont(footerFont);
		const QFontMetrics footerMetrics(footerFont);
		const QRect footerRect(state.rect.left() + 10, footerTop + 1, state.rect.width() - 20, 16);
		// The span arrives finished, in the metric's own unit; the board only
		// puts it in board case.
		const QString spanText = state.spanValueText.toUpper();
		painter.setPen(mutedInk);
		painter.drawText(footerRect, Qt::AlignRight | Qt::AlignVCenter, spanText);
		const QString marker = QStringLiteral("> ");
		painter.drawText(footerRect, Qt::AlignLeft | Qt::AlignVCenter, marker);
		const int channelX = footerRect.left() + footerMetrics.horizontalAdvance(marker);
		const int channelAvail = footerRect.right() - footerMetrics.horizontalAdvance(spanText) - 12 - channelX;
		painter.setPen(mixColor(mutedInk, textInk, state.hover));
		painter.drawText(QRect(channelX, footerRect.top(), qMax(0, channelAvail), footerRect.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			footerMetrics.elidedText(state.channelText, Qt::ElideRight, qMax(0, channelAvail)));

		// The data cell's outer 1px rule.
		painter.setPen(QPen(borderInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
	}

	// A row of mutually exclusive choices as a patch bus. The cells are ports
	// on a board: a 1px rule divides them, a bus lane runs under them, and the
	// choice is a lit block plugged into one port - the picker's engagement
	// grammar (LED fill + patch trace that makes the routing literally
	// visible) folded into a 24px strip. That lane is what keeps the control
	// from reading as an ordinary board cell that happens to sit in a row: an
	// ordinary cell has no bus under it and nothing travelling along it.
	//
	// The block reads state.selectionPosition, so running through three
	// choices is one patch walking the bus. It is snapped to whole pixels
	// (rule 7 - a board steps, it does not glide), and the labels are drawn
	// twice under complementary clips, so a label the block is crossing is lit
	// exactly as far as the block has reached instead of fading through a
	// half-ink nobody chose.
	void paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const override
	{
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const QRect frame = state.rect;

		QPainterStateGuard painterState(&painter);
		// Invariant rule 7: no antialiasing under a 1px rule. Nothing here is
		// a curve, so only the type is smoothed.
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		// Sunken board ground, the recess every readout cell sits in.
		painter.fillRect(frame, QColor(tokens.surfaceSunken));
		if (state.labels.isEmpty() || frame.width() < 8 || frame.height() < 8)
		{
			painter.setPen(QPen(borderInk, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(frame.adjusted(0, 0, -1, -1));
			return;
		}

		// Cancelled: every slot stays on the board and loses its light. The
		// contents sink to low alpha; the outer rule takes the cancellation
		// dash at full ink further down.
		if (!state.enabled)
			painter.setOpacity(0.55);

		// The bus lane takes the bottom of the strip and the ports sit above
		// it. A strip too short for both keeps the ports and drops the lane
		// rather than crushing the two together.
		const bool hasLane = frame.height() >= 18;
		const int laneY = hasLane ? frame.bottom() - 3 : frame.bottom() + 2;
		const int cellTop = frame.top() + 2;
		const int cellBottom = laneY - 2;
		const int cellHeight = qMax(1, cellBottom - cellTop);
		const int lastIndex = static_cast<int>(state.labels.size()) - 1;
		// Both edges are rounded from the fractional cell, never the width
		// from one edge: that is what makes the ports tile the strip exactly
		// instead of leaving a seam that drifts along the bus.
		const auto columnSpan = [&](double index, int inset) {
			const QRectF seg = state.segmentRect(index);
			const int left = qRound(seg.left()) + inset;
			const int right = qRound(seg.right()) - inset;
			return QRect(left, cellTop, qMax(1, right - left), cellHeight);
		};
		const auto columnRect = [&](double index) { return columnSpan(index, 0); };

		// A cancelled strip answers to nothing: the pointer states are dropped
		// here once instead of being tested at every use below.
		const int hovered = state.enabled && state.hoveredIndex >= 0 && state.hoveredIndex <= lastIndex
			? state.hoveredIndex : -1;
		const int pressed = state.enabled && state.pressedIndex >= 0 && state.pressedIndex <= lastIndex
			? state.pressedIndex : -1;

		// Crosspoint pre-light: addressing a port lights the whole bus faintly
		// (the row band) and the addressed column firmly, so the cell reads as
		// an intersection rather than as a button. The column band carries far
		// more alpha than the picker's original 16-18, which measured about
		// 3.5% brightness on a real panel and was reported as "hover does not
		// highlight" (M2 recalibration).
		if (hovered >= 0)
		{
			painter.fillRect(frame.adjusted(1, 1, -1, -1), withAlpha(accent, 14));
			painter.fillRect(columnRect(hovered).adjusted(1, -1, -1, 1), withAlpha(accent, 40));
		}

		// The port dividers and the bus rule: crisp integer 1px board ruling.
		painter.setPen(QPen(borderInk, 1));
		for (int i = 1; i <= lastIndex; i++)
		{
			const int x = qRound(state.segmentRect(i).left());
			painter.drawLine(x, frame.top() + 1, x, hasLane ? laneY + 2 : frame.bottom() - 1);
		}
		if (hasLane)
			painter.drawLine(frame.left() + 1, laneY, frame.right() - 1, laneY);

		// The bus ladder: a resting port is border ink, the addressed port
		// takes accent at 1px, the engaged port takes accent at 2px. Three
		// ranks that cannot be mistaken for one another at arm's length.
		if (hasLane && hovered >= 0)
		{
			const QRect column = columnRect(hovered);
			painter.setPen(QPen(withAlpha(accent, 120), 1));
			painter.drawLine(column.left() + 1, laneY, column.right() - 1, laneY);
		}

		// Press is the engage preview: the 1px accent rule plus a fill well
		// clear of the pre-light, one step short of the lit block it is about
		// to become.
		if (pressed >= 0 && pressed != state.selectedIndex)
		{
			const QRect column = columnRect(pressed).adjusted(1, -1, -1, 1);
			painter.fillRect(column, withAlpha(accent, 90));
			painter.setPen(QPen(accent, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(column.adjusted(0, 0, -1, -1));
		}

		// The engaged port. Whole-pixel geometry so the fill and the label
		// clip below agree exactly and the travel steps instead of blurring.
		const double position = qBound(0.0, state.selectionPosition, double(lastIndex));
		const QRect mark = columnSpan(position, 2);
		QColor litInk(accent);
		// Pressing the port that is already engaged still answers: the lamp
		// brightens by the skin's own step rather than doing nothing.
		if (pressed >= 0 && pressed == state.selectedIndex)
			litInk = litInk.lighter(115);
		if (state.enabled)
		{
			painter.fillRect(mark, litInk);
		}
		else
		{
			// Hollow lamp: the choice stays posted, unlit.
			painter.setPen(QPen(mutedInk, 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(mark.adjusted(0, 0, -1, -1));
		}

		// The patch: the block's footprint on the bus and the 1px drop that
		// plugs one into the other.
		if (hasLane)
		{
			const QColor patchInk = state.enabled ? litInk : mutedInk;
			painter.setPen(QPen(patchInk, 1));
			painter.drawLine(mark.center().x(), mark.bottom() + 1, mark.center().x(), laneY - 1);
			if (state.enabled)
				painter.fillRect(QRect(mark.left(), laneY, mark.width(), 2), patchInk);
			else
				painter.drawLine(mark.left(), laneY, mark.right(), laneY);
		}

		// Board type: one size and one weight in every cell (rank comes from
		// position and light, never from size), all caps, elided rather than
		// squeezed.
		QFont cellFont(tokens.monoFontFamily);
		cellFont.setPointSizeF(7.5);
		cellFont.setWeight(QFont::DemiBold);
		cellFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
		painter.setFont(cellFont);
		const QFontMetrics cellMetrics(cellFont);
		const auto drawLabels = [&](bool lit) {
			for (int i = 0; i <= lastIndex; i++)
			{
				const QRect column = columnRect(i);
				// A cancelled strip keeps its figures readable and lets the
				// opacity say it is cancelled; only a live board lights ink.
				QColor ink = mutedInk;
				if (state.enabled)
				{
					if (lit)
						ink = QColor(tokens.background);
					else if (i == hovered || i == pressed)
						ink = textInk;
				}
				painter.setPen(ink);
				painter.drawText(column, Qt::AlignCenter,
					cellMetrics.elidedText(state.labels.at(i).toUpper(), Qt::ElideRight,
						qMax(0, column.width() - 8)));
			}
		};
		{
			QPainterStateGuard unlitLabelState(&painter);
			QRegion unlit(frame);
			if (state.enabled)
				unlit -= QRegion(mark);
			painter.setClipRegion(unlit);
			drawLabels(false);
		}
		if (state.enabled)
		{
			QPainterStateGuard litLabelState(&painter);
			painter.setClipRect(mark);
			drawLabels(true);
		}

		// Keyboard focus brackets the engaged port at the port's own edges -
		// square corners, never a glow (the corner language is the
		// rectangle), and it travels with the patch.
		if (state.focused && state.enabled)
		{
			QRect bracket = columnRect(position).adjusted(0, -1, 0, 1);
			// The end ports would otherwise put their corners on the outer
			// rule, which paints last and would swallow them.
			bracket.setLeft(qMax(bracket.left(), frame.left() + 1));
			bracket.setRight(qMin(bracket.right(), frame.right() - 1));
			bracket.setTop(qMax(bracket.top(), frame.top() + 1));
			bracket.setBottom(qMin(bracket.bottom(), frame.bottom() - 1));
			const int leg = qBound(3, bracket.width() / 6, 6);
			painter.setPen(QPen(accent, 1));
			painter.drawLine(bracket.left(), bracket.top(), bracket.left() + leg, bracket.top());
			painter.drawLine(bracket.left(), bracket.top(), bracket.left(), bracket.top() + leg);
			painter.drawLine(bracket.right() - leg, bracket.top(), bracket.right(), bracket.top());
			painter.drawLine(bracket.right(), bracket.top(), bracket.right(), bracket.top() + leg);
			painter.drawLine(bracket.left(), bracket.bottom() - leg, bracket.left(), bracket.bottom());
			painter.drawLine(bracket.left(), bracket.bottom(), bracket.left() + leg, bracket.bottom());
			painter.drawLine(bracket.right(), bracket.bottom() - leg, bracket.right(), bracket.bottom());
			painter.drawLine(bracket.right() - leg, bracket.bottom(), bracket.right(), bracket.bottom());
		}

		// The strip's outer rule; a cancelled control cancels it with a dash
		// at full ink (the dash itself is never dimmed).
		painter.setOpacity(1.0);
		painter.setPen(QPen(borderInk, 1, state.enabled ? Qt::SolidLine : Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(frame.adjusted(0, 0, -1, -1));
	}

	// The board's masthead: the faint 24px column grid behind the title
	// readout and a doubled bottom rule (this inner line plus the QSS
	// bottom border). The caption cells stay transparent in QSS so the grid
	// runs through them.
	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override
	{
		painter.setRenderHint(QPainter::Antialiasing, false);

		// The hook carries no mode flag; infer it from the surface lightness
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		QColor grid(tokens.border);
		grid.setAlpha(tokens.dark ? 55 : 90);
		painter.setPen(QPen(grid, 1));
		for (int x = rect.left() + MatrixMetrics::gridPitch; x < rect.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, rect.top(), x, rect.bottom());

		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(rect.left(), rect.bottom() - 3, rect.right(), rect.bottom() - 3);
	}

	// The QSS dresses every toolbar item as a square 1px cell; two painted
	// layers add what QSS cannot express: the 24px column grid behind the
	// cells and the status lamp inside the DirtyStatusBadge readout. Runs
	// at startup and on every skin/dark switch, so the layers are looked up
	// again and re-tinted, never created twice.
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
			board->refreshOverlay();
			return board;
		};
		boardLayer(QStringLiteral("MatrixToolbarBoardUnder"), MatrixToolbarBoard::UnderCells)->setBoardTokens(tokens);
		boardLayer(QStringLiteral("MatrixToolbarBoardOver"), MatrixToolbarBoard::OverCells)->setBoardTokens(tokens);
	}

	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override
	{
		// Navigation keeps the shared stroke set on phosphor ink; the entry
		// pictograms switch to the panel's chamfered CRT glyphs. The faint
		// board grid behind the views comes from the skin sheet
		// (QFileDialog-scoped rules in matrix_*.qss).
		ISkin::styleFileDialog(dialog, tokens);
		if (dialog == nullptr)
			return;
		static MatrixFileIconProvider iconProvider;
		iconProvider.updateTokens(tokens);
		dialog->setIconProvider(&iconProvider);
	}
};
}

ISkin* matrixSkin()
{
	static MatrixSkin instance;
	return &instance;
}
