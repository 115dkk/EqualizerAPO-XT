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
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/cards/MatrixReferenceCardView.h"
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
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const override
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

		const int unit = tokens.channelGroupIndent;
		const int h = size.height();
		// Branch/tail rows mount at member depth (logicSiblingsIndentAsMembers)
		// so the bracket passes them; the card edge follows the same rule.
		const int indentUnits = branchOrTail ? info.depth + 1 : info.depth;
		const int cardLeft = 8 + indentUnits * unit;
		// Rules sit on the centres of the existing indent bands; the bracket
		// claims no positions of its own.
		const auto bandCenter = [&](int level) { return 8 + level * unit + unit / 2; };

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
	// axis figures, 0 dB bus, accent trace over one echo stroke, amber
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
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(x, plot.top(), x, plot.bottom());
		}
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
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
			painter.save();
			painter.setClipRect(zone);
			painter.setPen(QPen(hatch, 1));
			// Diagonals on the board's 12px half-pitch (gridPitch 24 = two
			// rows of 12): thin rules that read individually, not a texture.
			for (int x = zone.left() - zone.height(); x <= zone.right(); x += 12)
				painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());

			// Where the trace actually exceeds the bus, the hazard densifies:
			// half-pitch diagonals at full caution ink, clipped to the area
			// between the trace and the 0 dB rule. The band says "this side
			// can clip"; the dense region says WHERE and BY HOW MUCH.
			if (state.curve.size() >= 2)
			{
				QPolygonF closed = state.curve;
				closed.append(QPointF(state.curve.last().x(), state.zeroY));
				closed.append(QPointF(state.curve.first().x(), state.zeroY));
				QPainterPath overshoot;
				overshoot.addPolygon(closed);
				overshoot.closeSubpath();
				painter.setClipPath(overshoot, Qt::IntersectClip);
				QColor dense(hazardInk);
				dense.setAlpha(darkBoard ? 165 : 190);
				painter.setPen(QPen(dense, 1));
				for (int x = zone.left() - zone.height(); x <= zone.right(); x += 5)
					painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());
			}
			painter.restore();
		}

		// The 0 dB bus: the board's reference rule, one rank of authority
		// above the grid.
		QColor zeroInk(textInk);
		zeroInk.setAlpha(180);
		painter.setPen(QPen(zeroInk, 1));
		painter.drawLine(plot.left(), zeroYpx, plot.right(), zeroYpx);

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

		// dB tags ride the left edge. When the fitted range packs the 6 dB
		// rules tighter than a figure, thin the tags anchored on the 0 dB bus
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
			painter.setPen(line.major ? mutedInk : minorLabelInk);
			painter.drawText(tagRect, Qt::AlignCenter, line.label);
		}

		// The response trace: an accent core over a single wider low-alpha
		// echo stroke. The curve is data, so it alone is antialiased.
		if (state.curve.size() >= 2)
		{
			painter.setRenderHint(QPainter::Antialiasing, true);
			QColor echo(accent);
			echo.setAlpha(70);
			painter.setPen(QPen(echo, 3));
			painter.drawPolyline(state.curve);
			painter.setPen(QPen(accent, 1));
			painter.drawPolyline(state.curve);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}

		// The clip peak wears an OVER tag: a boxed cell in caution amber
		// pinned to the highest point of the trace, tied down by a 1px tick.
		if (state.clipping && state.curve.size() >= 2)
		{
			QPointF peak = state.curve.first();
			for (const QPointF& point : state.curve)
			{
				if (point.y() < peak.y())
					peak = point;
			}
			if (peak.y() < state.zeroY)
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
		// caption in the toolbar caption grammar, not user data).
		QFont captionFont(tokens.monoFontFamily);
		captionFont.setPointSizeF(7.0);
		captionFont.setWeight(QFont::DemiBold);
		captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(captionFont);
		painter.setPen(mutedInk);
		painter.drawText(QRect(plot.left(), state.rect.top() + 2, qMax(0, plot.width()), 12),
			Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("RESPONSE"));

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
		const QString spanText = QStringLiteral("+%1 / %2 DB").arg(state.maxDb, 0, 'f', 0).arg(state.minDb, 0, 'f', 0);
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
