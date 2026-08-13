/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "Editor/widgets/FilterCardModel.h"
#include "MatrixSkinDetail.h"

namespace
{

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

}

QString MatrixSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
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

QString MatrixSkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
		Q_UNUSED(info);
		Q_UNUSED(tokens);
		return QStringLiteral("QWidget#FilterCardHeader { background: transparent; border-radius: 0px; }");
	}

BadgeTreatment MatrixSkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const
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

void MatrixSkin::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const
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
		if (FilterCardModel::hostsSharedRawBody(info.type, info.dynamicLine) && body != nullptr)
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

void MatrixSkin::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const
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

		// The VST body is one dark board panel. The reference view's port
		// strip and feed line stand on the window ground, but the layout
		// margins around the view let the card face peek through, which
		// read as a flake floating mid-card (r3 judging). Flooding the whole
		// body band with the same ground fuses decoration, feed line and
		// caption strip into one block.
		if (info.type == QStringLiteral("vst") && content.height() > headerHeight)
			painter.fillRect(QRect(content.left(), content.top() + headerHeight,
				content.width(), content.height() - headerHeight), QColor(tokens.background));

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

bool MatrixSkin::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const
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

bool MatrixSkin::logicSiblingsIndentAsMembers() const
{
		return true;
	}
