/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's bass-management card: a terminal-grid summary with an
	aggregate crosspoint board, coordinate readout and boxed mono facts.
*/

#include "MatrixBassManagementCardView.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"

namespace
{
constexpr int cardHorizontalMargin = 12;
constexpr int cardVerticalMargin = 10;
constexpr int boardFrameInset = 1;
constexpr int boardInnerPadding = 6;
constexpr int boardTopRailHeight = 20;
constexpr int boardLeftRailWidth = 40;
constexpr int boardColumnPitch = 40;
constexpr int boardRowPitch = 24;
constexpr int boardCellGap = 4;
constexpr int maximumVisibleRows = 8;
constexpr int boardFoldWidth = 420;
constexpr int secondaryFoldWidth = 680;
constexpr int actionTextWidthPerButton = 112;

double crispCoordinate(
	double coordinate,
	qreal devicePixelRatio)
{
	return (std::floor(coordinate * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
}

QPen crispPen(
	const QColor& color,
	qreal devicePixelRatio)
{
	QPen pen(color);
	pen.setWidthF(1.0 / devicePixelRatio);
	pen.setCosmetic(false);
	pen.setCapStyle(Qt::SquareCap);
	pen.setJoinStyle(Qt::MiterJoin);
	return pen;
}

QRectF crispStrokeRect(
	const QRectF& rectangle,
	qreal devicePixelRatio)
{
	const qreal pixel = 1.0 / devicePixelRatio;

	const qreal left = crispCoordinate(
		rectangle.left(),
		devicePixelRatio);
	const qreal top = crispCoordinate(
		rectangle.top(),
		devicePixelRatio);
	const qreal right = crispCoordinate(
		rectangle.right() - pixel,
		devicePixelRatio);
	const qreal bottom = crispCoordinate(
		rectangle.bottom() - pixel,
		devicePixelRatio);

	return QRectF(
		QPointF(left, top),
		QPointF(right, bottom));
}

QFont matrixMonoFont()
{
	QFont font(
		SkinManager::instance()->tokens().monoFontFamily);

	if (font.family().isEmpty())
		font.setStyleHint(QFont::Monospace);

	font.setFixedPitch(true);
	return font;
}

int nonNegative(int value)
{
	return qMax(0, value);
}

int boundedPathCount(
	const BassManagementCardState& state)
{
	const qint64 groupCount =
		static_cast<qint64>(nonNegative(
			state.speakerGroupCount));
	const qint64 bassCount =
		static_cast<qint64>(nonNegative(
			state.bassPathCount));
	const qint64 lfeCount =
		state.sourceLfePreserved ? 1 : 0;

	return static_cast<int>(
		qMin<qint64>(
			groupCount + bassCount + lfeCount,
			9999));
}

void repolish(QWidget* widget)
{
	if (widget == nullptr)
		return;

	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

void drawMeasuredText(
	QPainter& painter,
	const QRectF& rectangle,
	const QString& text,
	int alignment)
{
	if (rectangle.width() <= 0.0
		|| rectangle.height() <= 0.0
		|| text.isEmpty())
	{
		return;
	}

	const QFontMetricsF metrics(painter.font());

	if (metrics.height() > rectangle.height())
		return;

	QString displayedText = text;
	if (metrics.horizontalAdvance(displayedText)
		> rectangle.width())
	{
		displayedText = metrics.elidedText(
			displayedText,
			Qt::ElideRight,
			static_cast<int>(
				std::floor(rectangle.width())));
	}

	if (displayedText.isEmpty()
		|| metrics.horizontalAdvance(displayedText)
			> rectangle.width())
	{
		return;
	}

	painter.drawText(
		rectangle,
		alignment | Qt::TextSingleLine,
		displayedText);
}

QString translatedBoardText(const char* sourceText)
{
	return QCoreApplication::translate(
		"MatrixBassManagementCardView",
		sourceText);
}
}

class MatrixBassReadoutLabel : public QLabel
{
public:
	// The matrix sheets pad these readout boxes ~10 px per side; QWidget's
	// contents margins cannot see that, so both the size hint and the elision
	// budget it through this shared constant.
	static constexpr int kStyleSheetHorizontalPadding = 22;

	explicit MatrixBassReadoutLabel(
		const QString& descriptiveToolTip,
		QWidget* parent = nullptr)
		: QLabel(parent),
		  baseToolTip(descriptiveToolTip)
	{
		setTextFormat(Qt::PlainText);
		setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		setMinimumWidth(0);
	}

	void setReadoutText(const QString& text)
	{
		fullText = text;
		updateDisplayedText();
		updateToolTip();
		setAccessibleDescription(fullText);
		updateGeometry();
	}

	// The layout must budget for the full string, not for whatever elided
	// remnant is currently displayed - otherwise the first relayout shrinks
	// the label, the shorter width elides harder, and the cell collapses to
	// a lone ellipsis.
	QSize sizeHint() const override
	{
		const QFontMetrics metrics(font());
		const QMargins margins = contentsMargins();
		// Budget the same sheet padding the elision subtracts, so a cell that
		// gets its hinted width shows the full string, not an elided one.
		return QSize(
			metrics.horizontalAdvance(fullText)
				+ margins.left() + margins.right()
				+ kStyleSheetHorizontalPadding,
			qMax(QLabel::sizeHint().height(),
				metrics.height() + margins.top() + margins.bottom()));
	}

	QSize minimumSizeHint() const override
	{
		const QFontMetrics metrics(font());
		// Enough for a recognizable prefix; elision handles the rest.
		return QSize(
			metrics.horizontalAdvance(QStringLiteral("MMMMMM")),
			QLabel::minimumSizeHint().height());
	}

protected:
	bool event(QEvent* event) override
	{
		const bool result = QLabel::event(event);

		switch (event->type())
		{
		case QEvent::FontChange:
		case QEvent::StyleChange:
		case QEvent::Polish:
		case QEvent::LayoutDirectionChange:
			updateDisplayedText();
			break;

		default:
			break;
		}

		return result;
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		updateDisplayedText();
	}

private:
	void updateDisplayedText()
	{
		// contentsRect() does not see QSS padding (the style sheet box model
		// lives in the style, not in QWidget's contents margins), so eliding
		// against it overflows into the padded border by a glyph. Budget for
		// the sheet's horizontal padding explicitly.
		const int availableWidth = qMax(0,
			contentsRect().width() - kStyleSheetHorizontalPadding);

		QString displayedText = fullText;
		if (availableWidth > 0)
		{
			const QFontMetrics metrics(font());
			displayedText = metrics.elidedText(
				fullText,
				Qt::ElideRight,
				availableWidth);
		}

		QLabel::setText(displayedText);
	}

	void updateToolTip()
	{
		if (baseToolTip.isEmpty())
		{
			QLabel::setToolTip(fullText);
			return;
		}

		if (fullText.isEmpty())
		{
			QLabel::setToolTip(baseToolTip);
			return;
		}

		QLabel::setToolTip(
			baseToolTip
				+ QLatin1Char('\n')
				+ fullText);
	}

	QString baseToolTip;
	QString fullText;
};

namespace
{
void setReadoutText(
	QLabel* label,
	const QString& text)
{
	if (MatrixBassReadoutLabel* readout =
		dynamic_cast<MatrixBassReadoutLabel*>(label))
	{
		readout->setReadoutText(text);
		return;
	}

	label->setText(text);
}
}

class MatrixBassManagementBoard : public QWidget
{
public:
	explicit MatrixBassManagementBoard(
		QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(
			QStringLiteral("MatrixBassCrosspointBoard"));
		setSizePolicy(
			QSizePolicy::Expanding,
			QSizePolicy::Fixed);
		setAccessibleName(
			translatedBoardText(
				"Bass-management crosspoint board"));
		setToolTip(
			translatedBoardText(
				"Aggregate routing presence by path row and physical-output column"));
	}

	void setCardState(
		const BassManagementCardState& newState)
	{
		cardState = newState;
		recalculateCardGeometry();
		recalculateDisplayGeometry(
			viewportWidth > 0
				? viewportWidth
				: width());
		updateAccessibleDescription();
		updateGeometry();
		update();
	}

	void setViewportWidth(int availableWidth)
	{
		const int boundedWidth =
			qMax(0, availableWidth);

		if (viewportWidth == boundedWidth)
			return;

		viewportWidth = boundedWidth;
		recalculateDisplayGeometry(viewportWidth);
		updateAccessibleDescription();
		update();
	}

	int visibleRowCount() const
	{
		return displayedRows;
	}

	int visibleColumnCount() const
	{
		return displayedColumns;
	}

	int fullRowCount() const
	{
		return completeRows;
	}

	int fullColumnCount() const
	{
		return completeColumns;
	}

	bool isTruncated() const
	{
		return displayedRows < completeRows
			|| displayedColumns < completeColumns;
	}

	qint64 fullCapacity() const
	{
		return static_cast<qint64>(completeRows)
			* static_cast<qint64>(completeColumns);
	}

	QSize sizeHint() const override
	{
		const int representativeColumns =
			qMin(completeColumns, 12);
		const int desiredWidth =
			boardFrameInset * 2
				+ boardInnerPadding * 2
				+ boardLeftRailWidth
				+ representativeColumns
					* boardColumnPitch;
		const int desiredHeight =
			boardFrameInset * 2
				+ boardInnerPadding * 2
				+ boardTopRailHeight
				+ displayedRows * boardRowPitch;

		return QSize(
			qMax(480, desiredWidth),
			desiredHeight);
	}

	QSize minimumSizeHint() const override
	{
		return QSize(
			240,
			boardFrameInset * 2
				+ boardInnerPadding * 2
				+ boardTopRailHeight
				+ displayedRows * boardRowPitch);
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		painter.setRenderHint(
			QPainter::Antialiasing,
			false);
		painter.setRenderHint(
			QPainter::TextAntialiasing,
			true);

		const qreal devicePixelRatio =
			painter.device()->devicePixelRatioF();
		const QPalette::ColorGroup colorGroup =
			isEnabled()
				? QPalette::Active
				: QPalette::Disabled;
		const QPalette boardPalette = palette();

		const QColor base = boardPalette.color(
			colorGroup,
			QPalette::Window);
		const QColor text = boardPalette.color(
			colorGroup,
			QPalette::Text);
		const QColor muted = boardPalette.color(
			colorGroup,
			QPalette::PlaceholderText);
		const QColor border = boardPalette.color(
			colorGroup,
			QPalette::Mid);
		const QColor highlightedText =
			boardPalette.color(
				colorGroup,
				QPalette::HighlightedText);
		const QColor accent =
			SkinManager::instance()->tokens().accent;

		painter.fillRect(rect(), base);

		painter.setPen(
			crispPen(border, devicePixelRatio));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(
			crispStrokeRect(
				QRectF(rect()),
				devicePixelRatio));

		const qreal usableWidth =
			width()
				- boardFrameInset * 2
				- boardInnerPadding * 2;
		const qreal clusterWidth =
			boardLeftRailWidth
				+ displayedColumns
					* boardColumnPitch;
		const qreal clusterLeft =
			qMax<qreal>(
				boardFrameInset
					+ boardInnerPadding,
				(width() - clusterWidth) / 2.0);
		const qreal railTop =
			boardFrameInset + boardInnerPadding;
		const qreal gridLeft =
			clusterLeft + boardLeftRailWidth;
		const qreal gridTop =
			railTop + boardTopRailHeight;
		const qreal gridRight =
			gridLeft
				+ displayedColumns
					* boardColumnPitch;
		const qreal gridBottom =
			gridTop
				+ displayedRows
					* boardRowPitch;

		Q_UNUSED(usableWidth)

		QFont labelFont = matrixMonoFont();
		labelFont.setPointSize(7);
		painter.setFont(labelFont);
		painter.setPen(muted);

		drawMeasuredText(
			painter,
			QRectF(
				clusterLeft,
				railTop,
				boardLeftRailWidth,
				boardTopRailHeight),
			translatedBoardText("R/O"),
			Qt::AlignCenter);

		if (displayedRows <= 0
			|| displayedColumns <= 0)
		{
			drawMeasuredText(
				painter,
				QRectF(
					clusterLeft,
					gridTop,
					qMax<qreal>(
						boardLeftRailWidth,
						width()
							- clusterLeft
							- boardInnerPadding
							- boardFrameInset),
					boardRowPitch),
				translatedBoardText(
					"-- NO PATHS --"),
				Qt::AlignCenter);
			return;
		}

		painter.setPen(
			crispPen(border, devicePixelRatio));

		const qreal verticalRailX =
			crispCoordinate(
				gridLeft,
				devicePixelRatio);
		painter.drawLine(
			QPointF(verticalRailX, railTop),
			QPointF(verticalRailX, gridBottom));

		const qreal horizontalRailY =
			crispCoordinate(
				gridTop,
				devicePixelRatio);
		painter.drawLine(
			QPointF(clusterLeft, horizontalRailY),
			QPointF(gridRight, horizontalRailY));

		for (int column = 0;
			column < displayedColumns;
			++column)
		{
			const qreal centerX =
				crispCoordinate(
					gridLeft
						+ column
							* boardColumnPitch
						+ boardColumnPitch
							/ 2.0,
					devicePixelRatio);

			painter.drawLine(
				QPointF(centerX, gridTop),
				QPointF(centerX, gridBottom));
		}

		for (int row = 0;
			row < displayedRows;
			++row)
		{
			const qreal centerY =
				crispCoordinate(
					gridTop
						+ row * boardRowPitch
						+ boardRowPitch / 2.0,
					devicePixelRatio);

			painter.drawLine(
				QPointF(clusterLeft, centerY),
				QPointF(gridRight, centerY));
		}

		painter.setPen(muted);

		for (int column = 0;
			column < displayedColumns;
			++column)
		{
			const QString columnLabel =
				translatedBoardText("O%1")
					.arg(
						column + 1,
						2,
						10,
						QLatin1Char('0'));

			drawMeasuredText(
				painter,
				QRectF(
					gridLeft
						+ column
							* boardColumnPitch,
					railTop,
					boardColumnPitch,
					boardTopRailHeight),
				columnLabel,
				Qt::AlignCenter);
		}

		for (int row = 0;
			row < displayedRows;
			++row)
		{
			const QString rowLabel =
				translatedBoardText("R%1")
					.arg(
						row + 1,
						2,
						10,
						QLatin1Char('0'));

			drawMeasuredText(
				painter,
				QRectF(
					clusterLeft,
					gridTop
						+ row * boardRowPitch,
					boardLeftRailWidth,
					boardRowPitch),
				rowLabel,
				Qt::AlignCenter);
		}

		painter.setPen(
			crispPen(border, devicePixelRatio));
		painter.setBrush(Qt::NoBrush);

		for (int row = 0;
			row < displayedRows;
			++row)
		{
			for (int column = 0;
				column < displayedColumns;
				++column)
			{
				const QRectF cellRectangle(
					gridLeft
						+ column
							* boardColumnPitch
						+ boardCellGap / 2.0,
					gridTop
						+ row * boardRowPitch
						+ boardCellGap / 2.0,
					boardColumnPitch
						- boardCellGap,
					boardRowPitch
						- boardCellGap);

				painter.drawRect(
					crispStrokeRect(
						cellRectangle,
						devicePixelRatio));
			}
		}

		const int visibleCapacity =
			displayedRows * displayedColumns;
		if (visibleCapacity <= 0
			|| cardState.activeMatrixEdges <= 0
			|| fullCapacity() <= 0)
		{
			return;
		}

		const double density = std::clamp(
			static_cast<double>(
				cardState.activeMatrixEdges)
				/ static_cast<double>(
					fullCapacity()),
			0.0,
			1.0);

		int visibleActiveCount =
			static_cast<int>(
				std::round(
					density
						* visibleCapacity));
		visibleActiveCount = std::clamp(
			visibleActiveCount,
			1,
			visibleCapacity);

		int stride = 1;
		for (int candidate = qMin(
				7,
				visibleCapacity - 1);
			candidate < visibleCapacity;
			++candidate)
		{
			if (std::gcd(
				candidate,
				visibleCapacity) == 1)
			{
				stride = candidate;
				break;
			}
		}

		const int startIndex =
			(nonNegative(cardState.activeMatrixEdges)
				+ completeRows
				+ completeColumns)
			% visibleCapacity;

		QVector<bool> activeCells(
			visibleCapacity,
			false);

		for (int activeIndex = 0;
			activeIndex < visibleActiveCount;
			++activeIndex)
		{
			const int cellIndex =
				(startIndex
					+ activeIndex * stride)
				% visibleCapacity;
			activeCells[cellIndex] = true;
		}

		QFont digitFont = matrixMonoFont();
		digitFont.setPointSize(7);
		digitFont.setWeight(QFont::DemiBold);
		painter.setFont(digitFont);

		for (int row = 0;
			row < displayedRows;
			++row)
		{
			for (int column = 0;
				column < displayedColumns;
				++column)
			{
				const int cellIndex =
					row * displayedColumns
						+ column;

				if (!activeCells.at(cellIndex))
					continue;

				const QRectF activeRectangle(
					gridLeft
						+ column
							* boardColumnPitch
						+ boardCellGap / 2.0,
					gridTop
						+ row * boardRowPitch
						+ boardCellGap / 2.0,
					boardColumnPitch
						- boardCellGap,
					boardRowPitch
						- boardCellGap);

				painter.fillRect(
					activeRectangle,
					accent);

				double factorDb = 0.0;
				const bool visibleLfeRow =
					cardState.sourceLfePreserved
						&& row
							== completeRows - 1;

				if (visibleLfeRow
					&& std::isfinite(
						cardState.sourceLfeGainDb))
				{
					factorDb =
						cardState.sourceLfeGainDb;
				}

				const QString factorText =
					translatedBoardText("%1dB")
						.arg(
							QString::number(
								factorDb,
								'f',
								1));

				const QFontMetricsF digitMetrics(
					painter.font());
				const QRectF digitRectangle =
					activeRectangle.adjusted(
						2.0,
						1.0,
						-2.0,
						-1.0);

				if (digitMetrics.height()
						<= digitRectangle.height()
					&& digitMetrics.horizontalAdvance(
						factorText)
						<= digitRectangle.width())
				{
					painter.setPen(highlightedText);
					drawMeasuredText(
						painter,
						digitRectangle,
						factorText,
						Qt::AlignCenter);
				}
			}
		}

		painter.setPen(text);
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);

		if (viewportWidth <= 0)
		{
			recalculateDisplayGeometry(
				event->size().width());
			updateAccessibleDescription();
		}

		update();
	}

private:
	void recalculateCardGeometry()
	{
		completeRows = boundedPathCount(cardState);
		completeColumns =
			physicalOutputCount(cardState);
		displayedRows =
			qMin(completeRows, maximumVisibleRows);
	}

	void recalculateDisplayGeometry(
		int availableWidth)
	{
		const int gridWidth =
			qMax(
				0,
				availableWidth
					- boardFrameInset * 2
					- boardInnerPadding * 2
					- boardLeftRailWidth);
		const int availableColumns =
			qMax(1, gridWidth / boardColumnPitch);

		displayedColumns =
			qMin(
				completeColumns,
				availableColumns);
	}

	void updateAccessibleDescription()
	{
		setAccessibleDescription(
			translatedBoardText(
				"%1 visible path rows of %2, "
				"%3 visible output columns of %4, "
				"and %5 active matrix routes. "
				"Active-cell dB digits are representative aggregate annotations.")
				.arg(displayedRows)
				.arg(completeRows)
				.arg(displayedColumns)
				.arg(completeColumns)
				.arg(
					nonNegative(
						cardState.activeMatrixEdges)));
	}

	int physicalOutputCount(
		const BassManagementCardState& state) const
	{
		static const QRegularExpression layoutPattern(
			QStringLiteral(
				R"((\d+)(?:\.(\d+))?(?:\.(\d+))?)"));

		const QRegularExpressionMatch match =
			layoutPattern.match(state.layoutLabel);

		if (match.hasMatch())
		{
			int sum = 0;
			for (int captureIndex = 1;
				captureIndex <= 3;
				++captureIndex)
			{
				const QString captured =
					match.captured(captureIndex);

				if (!captured.isEmpty())
					sum += captured.toInt();
			}

			if (sum > 0)
				return qMin(64, sum);
		}

		const qint64 fallback =
			static_cast<qint64>(
				nonNegative(
					state.speakerGroupCount))
				+ qMax(
					1,
					nonNegative(
						state.bassPathCount));

		return static_cast<int>(
			qBound<qint64>(
				qint64(2),
				qint64(fallback),
				qint64(64)));
	}

	BassManagementCardState cardState;
	int completeRows = 0;
	int completeColumns = 2;
	int displayedRows = 0;
	int displayedColumns = 2;
	int viewportWidth = 0;
};

MatrixBassManagementCardView::
	MatrixBassManagementCardView(
		QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(
		QStringLiteral(
			"MatrixBassManagementCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(
		cardHorizontalMargin,
		cardVerticalMargin,
		cardHorizontalMargin,
		cardVerticalMargin);
	root->setSpacing(8);

	summaryStrip = new QWidget(this);
	summaryStrip->setObjectName(
		QStringLiteral("MatrixBassSummaryStrip"));

	QHBoxLayout* summaryLayout =
		new QHBoxLayout(summaryStrip);
	summaryLayout->setContentsMargins(0, 0, 0, 0);
	summaryLayout->setSpacing(6);

	validityCell = createReadoutCell(
		QStringLiteral("MatrixBassValidityCell"),
		tr("Bass-management validity state"),
		tr("Valid or error state indicator"));
	validityCell->setSizePolicy(
		QSizePolicy::Minimum,
		QSizePolicy::Preferred);
	summaryLayout->addWidget(validityCell);

	layoutCell = createReadoutCell(
		QStringLiteral("MatrixBassLayoutCell"),
		tr("Speaker layout"),
		tr("Physical speaker layout"));
	summaryLayout->addWidget(layoutCell, 1);

	crossoverCell = createReadoutCell(
		QStringLiteral("MatrixBassCrossoverCell"),
		tr("Representative crossovers"),
		tr("High-pass and low-pass crossover sections"));
	summaryLayout->addWidget(crossoverCell, 1);

	root->addWidget(summaryStrip);

	board = new MatrixBassManagementBoard(this);
	root->addWidget(board);

	coordinateLine =
		new MatrixBassReadoutLabel(
			tr("Visible grid coordinates, full dimensions and route capacity"),
			this);
	coordinateLine->setObjectName(
		QStringLiteral("MatrixBassCoordinateLine"));
	coordinateLine->setAccessibleName(
		tr("Coordinate and grid information"));
	root->addWidget(coordinateLine);

	secondaryStrip = new QWidget(this);
	secondaryStrip->setObjectName(
		QStringLiteral("MatrixBassSecondaryStrip"));

	QHBoxLayout* secondaryLayout =
		new QHBoxLayout(secondaryStrip);
	secondaryLayout->setContentsMargins(0, 0, 0, 0);
	secondaryLayout->setSpacing(6);

	pathsCell = createReadoutCell(
		QStringLiteral("MatrixBassPathsCell"),
		tr("Path and edge counts"),
		tr("Speaker groups, bass paths and matrix routes"));
	secondaryLayout->addWidget(pathsCell);

	sourceLfeCell = createReadoutCell(
		QStringLiteral("MatrixBassSourceLfeCell"),
		tr("Source LFE routing"),
		tr("LFE preservation and gain adjustment"));
	secondaryLayout->addWidget(sourceLfeCell);

	headroomCell = createReadoutCell(
		QStringLiteral("MatrixBassHeadroomCell"),
		tr("Bass-management headroom"),
		tr("Computed automatic or manual headroom trim"));
	secondaryLayout->addWidget(headroomCell, 1);

	profileCell = createReadoutCell(
		QStringLiteral("MatrixBassProfileCell"),
		tr("Bass-management profile"),
		tr("Embedded profile or linked profile name"));
	secondaryLayout->addWidget(profileCell, 1);

	root->addWidget(secondaryStrip);

	statusLine = new QLabel(this);
	statusLine->setObjectName(
		QStringLiteral("MatrixBassStatusLine"));
	statusLine->setTextFormat(Qt::PlainText);
	statusLine->setWordWrap(true);
	statusLine->setVisible(false);
	statusLine->setAccessibleName(
		tr("Status messages and alerts"));
	root->addWidget(statusLine);

	actionRow = new QWidget(this);
	actionRow->setObjectName(
		QStringLiteral("MatrixBassActionRow"));
	actionRow->setVisible(false);

	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	actionLayout->addStretch();

	root->addWidget(actionRow);

	connect(
		SkinManager::instance(),
		&SkinManager::skinChanged,
		this,
		[this]()
	{
		board->update();
		updateCoordinateLine(state());
		update();
	});

	// Qualified on purpose: seeding the initial presentation from the
	// constructor must not dispatch to a further-derived override.
	MatrixBassManagementCardView::applyState(state());
}

QLabel*
	MatrixBassManagementCardView::createReadoutCell(
		const QString& objectName,
		const QString& accessibleName,
		const QString& toolTip)
{
	MatrixBassReadoutLabel* cell =
		new MatrixBassReadoutLabel(toolTip, this);
	cell->setObjectName(objectName);
	cell->setAttribute(
		Qt::WA_StyledBackground,
		true);
	cell->setAccessibleName(accessibleName);
	return cell;
}

void MatrixBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setObjectName(
		QStringLiteral("MatrixBassActionButton"));
	button->setMinimumSize(40, 40);
	button->setSizePolicy(
		QSizePolicy::Minimum,
		QSizePolicy::Fixed);

	QString actionName = button->text();
	actionName.remove(QLatin1Char('&'));

	if (button->accessibleName().isEmpty()
		&& !actionName.isEmpty())
	{
		button->setAccessibleName(actionName);
	}

	if (button->toolTip().isEmpty()
		&& !actionName.isEmpty())
	{
		button->setToolTip(actionName);
	}

	if (QToolButton* toolButton =
		qobject_cast<QToolButton*>(button))
	{
		toolButton->setAutoRaise(false);
		actionButtons.append(toolButton);

		connect(
			toolButton,
			&QObject::destroyed,
			this,
			[this, toolButton]()
		{
			actionButtons.removeAll(toolButton);
			updateActionButtonStyles();
			actionRow->setVisible(
				!actionButtons.isEmpty());
		});
	}

	actionLayout->addWidget(button);
	actionRow->setVisible(true);
	updateActionButtonStyles();
}

void MatrixBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	setReadoutText(
		validityCell,
		state.valid
			? tr("+ STATE | VALID")
			: tr("X STATE | ERROR"));
	validityCell->setProperty(
		"severity",
		state.valid
			? QStringLiteral("valid")
			: QStringLiteral("critical"));
	repolish(validityCell);

	setReadoutText(
		layoutCell,
		state.layoutLabel.isEmpty()
			? tr("LAYOUT | -")
			: tr("LAYOUT | %1")
				.arg(state.layoutLabel));

	QStringList crossovers;
	if (!state.representativeHighPass.isEmpty())
	{
		crossovers.append(
			tr("HP %1").arg(
				state.representativeHighPass));
	}

	if (!state.representativeLowPass.isEmpty())
	{
		crossovers.append(
			tr("LP %1").arg(
				state.representativeLowPass));
	}

	setReadoutText(
		crossoverCell,
		crossovers.isEmpty()
			? tr("XOVER | -")
			: tr("XOVER | %1").arg(
				crossovers.join(tr(" / "))));

	setReadoutText(
		pathsCell,
		tr("%1 GRP | %2 BASS | %3 XPT")
			.arg(
				nonNegative(
					state.speakerGroupCount))
			.arg(
				nonNegative(
					state.bassPathCount))
			.arg(
				nonNegative(
					state.activeMatrixEdges)));

	if (state.sourceLfePreserved)
	{
		const QString lfeGain =
			std::isfinite(state.sourceLfeGainDb)
				? QString::number(
					state.sourceLfeGainDb,
					'f',
					1)
				: QStringLiteral("--");

		setReadoutText(
			sourceLfeCell,
			std::isfinite(state.sourceLfeGainDb)
				? tr("LFE | %1 dB").arg(lfeGain)
				: tr("LFE | %1").arg(lfeGain));
	}
	else
	{
		setReadoutText(
			sourceLfeCell,
			tr("LFE | OFF"));
	}

	if (std::isfinite(state.headroomTrimDb))
	{
		setReadoutText(
			headroomCell,
			state.headroomAuto
				? tr("HDROOM | AUTO %1 dB")
					.arg(
						QString::number(
							state.headroomTrimDb,
							'f',
							1))
				: tr("HDROOM | MANUAL %1 dB")
					.arg(
						QString::number(
							state.headroomTrimDb,
							'f',
							1)));
	}
	else
	{
		setReadoutText(
			headroomCell,
			state.headroomAuto
				? tr("HDROOM | AUTO --")
				: tr("HDROOM | MANUAL --"));
	}

	if (state.linkedProfile)
	{
		setReadoutText(
			profileCell,
			state.profileName.isEmpty()
				? tr("PROFILE | LINKED")
				: tr("PROFILE | LINKED %1")
					.arg(state.profileName));
	}
	else
	{
		setReadoutText(
			profileCell,
			state.profileName.isEmpty()
				? tr("PROFILE | EMBEDDED")
				: tr("PROFILE | %1")
					.arg(state.profileName));
	}

	board->setCardState(state);

	QStringList status;
	bool hasCriticalStatus = false;

	if (!state.errorText.isEmpty())
	{
		status.append(
			tr("X ERROR: %1")
				.arg(state.errorText));
		hasCriticalStatus = true;
	}

	if (!state.valid)
	{
		status.append(
			tr("X ERROR: State is invalid"));
		hasCriticalStatus = true;
	}

	if (state.linkedProfile
		&& state.profileMissing)
	{
		const QString profileName =
			state.profileName.isEmpty()
				? tr("unnamed")
				: state.profileName;

		status.append(
			tr("X ERROR: Linked profile \"%1\" is missing")
				.arg(profileName));
		hasCriticalStatus = true;
	}

	if (!state.warningText.isEmpty())
	{
		status.append(
			tr("! WARNING: %1")
				.arg(state.warningText));
	}

	const QString statusText =
		status.join(QLatin1Char('\n'));

	statusLine->setText(statusText);
	statusLine->setVisible(!status.isEmpty());
	statusLine->setToolTip(statusText);
	statusLine->setAccessibleDescription(statusText);
	statusLine->setProperty(
		"severity",
		hasCriticalStatus
			? QStringLiteral("critical")
			: status.isEmpty()
				? QStringLiteral("normal")
				: QStringLiteral("warning"));
	repolish(statusLine);

	updateResponsiveVisibility();
	update();
}

void MatrixBassManagementCardView::updateCoordinateLine(
	const BassManagementCardState& state)
{
	QStringList coordinate;

	if (board->fullRowCount() <= 0)
	{
		coordinate.append(
			tr("GRID: R-- / O01-O%1")
				.arg(
					board->visibleColumnCount(),
					2,
					10,
					QLatin1Char('0')));
	}
	else
	{
		coordinate.append(
			tr("GRID: R01-R%1 / O01-O%2")
				.arg(
					board->visibleRowCount(),
					2,
					10,
					QLatin1Char('0'))
				.arg(
					board->visibleColumnCount(),
					2,
					10,
					QLatin1Char('0')));
	}

	if (board->isTruncated())
	{
		coordinate.append(
			tr("VIEW %1x%2 / FULL %3x%4")
				.arg(board->visibleRowCount())
				.arg(board->visibleColumnCount())
				.arg(board->fullRowCount())
				.arg(board->fullColumnCount()));
	}

	coordinate.append(
		tr("ACTIVE %1 / %2")
			.arg(
				nonNegative(
					state.activeMatrixEdges))
			.arg(board->fullCapacity()));

	setReadoutText(
		coordinateLine,
		coordinate.join(QStringLiteral(" | ")));
}

void MatrixBassManagementCardView::paintEvent(
	QPaintEvent* event)
{
	BassManagementCardView::paintEvent(event);

	if (!hasFocus())
		return;

	QPainter painter(this);
	painter.setRenderHint(
		QPainter::Antialiasing,
		false);

	const qreal devicePixelRatio =
		painter.device()->devicePixelRatioF();
	painter.setPen(
		crispPen(
			palette().color(
				QPalette::Highlight),
			devicePixelRatio));

	const qreal left =
		crispCoordinate(
			1.0,
			devicePixelRatio);
	const qreal top =
		crispCoordinate(
			1.0,
			devicePixelRatio);
	const qreal right =
		crispCoordinate(
			width()
				- 1.0
				- 1.0 / devicePixelRatio,
			devicePixelRatio);
	const qreal bottom =
		crispCoordinate(
			height()
				- 1.0
				- 1.0 / devicePixelRatio,
			devicePixelRatio);
	const qreal bracket = 8.0;

	painter.drawLine(
		QPointF(left, top),
		QPointF(left + bracket, top));
	painter.drawLine(
		QPointF(left, top),
		QPointF(left, top + bracket));
	painter.drawLine(
		QPointF(right - bracket, top),
		QPointF(right, top));
	painter.drawLine(
		QPointF(right, top),
		QPointF(right, top + bracket));
	painter.drawLine(
		QPointF(left, bottom - bracket),
		QPointF(left, bottom));
	painter.drawLine(
		QPointF(left, bottom),
		QPointF(left + bracket, bottom));
	painter.drawLine(
		QPointF(right - bracket, bottom),
		QPointF(right, bottom));
	painter.drawLine(
		QPointF(right, bottom - bracket),
		QPointF(right, bottom));
}

void MatrixBassManagementCardView::resizeEvent(
	QResizeEvent* event)
{
	BassManagementCardView::resizeEvent(event);
	updateResponsiveVisibility();
}

void MatrixBassManagementCardView::
	updateResponsiveVisibility()
{
	const int availableWidth = width();
	const int boardWidth =
		qMax(
			0,
			availableWidth
				- cardHorizontalMargin * 2);

	board->setViewportWidth(boardWidth);

	const bool showBoard =
		availableWidth >= boardFoldWidth;
	board->setVisible(showBoard);
	coordinateLine->setVisible(showBoard);

	secondaryStrip->setVisible(
		availableWidth >= secondaryFoldWidth);

	updateCoordinateLine(state());
	updateActionButtonStyles();
}

void MatrixBassManagementCardView::
	updateActionButtonStyles()
{
	if (actionButtons.isEmpty())
		return;

	const int availableWidth =
		qMax(
			0,
			width()
				- cardHorizontalMargin * 2);
	const bool showActionText =
		availableWidth
			>= actionButtons.size()
				* actionTextWidthPerButton;

	for (QToolButton* button : actionButtons)
	{
		if (button == nullptr)
			continue;

		if (button->icon().isNull())
		{
			button->setToolButtonStyle(
				Qt::ToolButtonTextOnly);
		}
		else if (showActionText)
		{
			button->setToolButtonStyle(
				Qt::ToolButtonTextBesideIcon);
		}
		else
		{
			button->setToolButtonStyle(
				Qt::ToolButtonIconOnly);
		}
	}
}
