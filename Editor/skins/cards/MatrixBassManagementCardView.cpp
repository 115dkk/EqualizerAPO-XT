/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's bass-management card: a terminal-grid summary with an
	aggregate crosspoint board, coordinate readout and boxed mono facts.
*/

#include "MatrixBassManagementCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QVector>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"

namespace
{
constexpr int boardHeaderHeight = 18;
constexpr int boardRowHeight = 18;
constexpr int boardLabelWidth = 44;
constexpr int boardMargin = 5;
constexpr int narrowWidth = 520;
constexpr int secondaryFoldWidth = 680;

double crispCoordinate(double coordinate, qreal devicePixelRatio)
{
	return (std::floor(coordinate * devicePixelRatio) + 0.5)
		/ devicePixelRatio;
}

QPen crispPen(const QColor& color, qreal devicePixelRatio)
{
	QPen pen(color);
	pen.setWidthF(1.0 / devicePixelRatio);
	pen.setCosmetic(false);
	return pen;
}

QFont matrixMonoFont()
{
	QFont font(SkinManager::instance()->tokens().monoFontFamily);
	if (font.family().isEmpty())
		font.setStyleHint(QFont::Monospace);
	font.setFixedPitch(true);
	return font;
}

int nonNegative(int value)
{
	return qMax(0, value);
}
}

class MatrixBassManagementBoard : public QWidget
{
public:
	explicit MatrixBassManagementBoard(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("MatrixBassCrosspointBoard"));
		setAttribute(Qt::WA_OpaquePaintEvent, true);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setFont(matrixMonoFont());
		setAccessibleName(
			QCoreApplication::translate(
				"MatrixBassManagementCardView",
				"Bass-management crosspoint board"));
		setToolTip(
			QCoreApplication::translate(
				"MatrixBassManagementCardView",
				"Aggregate routing presence by signal path and physical-output slot"));
	}

	void setCardState(const BassManagementCardState& newState)
	{
		cardState = newState;
		recalculateGeometry();
		updateGeometry();
		update();

		setAccessibleDescription(
			QCoreApplication::translate(
				"MatrixBassManagementCardView",
				"%1 path rows, %2 output columns and %3 active matrix routes. "
				"Individual route factors are unavailable in the card state.")
				.arg(pathRowCount)
				.arg(outputColumns)
				.arg(nonNegative(cardState.activeMatrixEdges)));
	}

	int rowCount() const
	{
		return pathRowCount;
	}

	int columnCount() const
	{
		return outputColumns;
	}

	bool outputCountIsInferred() const
	{
		return inferredOutputCount;
	}

	int fullOutputCount(const BassManagementCardState& state) const
	{
		return physicalOutputCount(state);
	}

	QSize sizeHint() const override
	{
		return QSize(480,
			boardMargin * 2 + boardHeaderHeight
				+ pathRowCount * boardRowHeight);
	}

	QSize minimumSizeHint() const override
	{
		return QSize(240, sizeHint().height());
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		Q_UNUSED(event)

		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const qreal devicePixelRatio = painter.device()->devicePixelRatioF();
		const QPalette::ColorGroup colorGroup = isEnabled()
			? QPalette::Active
			: QPalette::Disabled;
		const QPalette boardPalette = palette();

		const QColor base = boardPalette.color(colorGroup, QPalette::Base);
		const QColor alternate = boardPalette.color(colorGroup, QPalette::AlternateBase);
		const QColor text = boardPalette.color(colorGroup, QPalette::Text);
		const QColor muted = boardPalette.color(colorGroup, QPalette::PlaceholderText);
		const QColor midTone = boardPalette.color(colorGroup, QPalette::Mid);
		const QColor accent = SkinManager::instance()->tokens().accent;

		painter.fillRect(rect(), base);

		if (pathRowCount <= 0 || outputColumns <= 0)
			return;

		const qreal left = boardMargin + boardLabelWidth;
		const qreal top = boardMargin + boardHeaderHeight;
		const qreal right = width() - boardMargin;
		const qreal bottom = height() - boardMargin;

		const qreal cellWidth = (right - left) / outputColumns;
		const qreal cellHeight = (bottom - top) / pathRowCount;

		// Draw alternating row backgrounds
		for (int row = 1; row < pathRowCount; ++row)
		{
			const qreal y = top + row * cellHeight;
			if ((row & 1) == 1)
			{
				painter.fillRect(
					QRectF(0, y, width(), cellHeight),
					alternate);
			}
		}

		// Draw grid lines
		painter.setPen(crispPen(midTone, devicePixelRatio));

		// Outer border
		painter.drawRect(QRectF(
			crispCoordinate(boardMargin, devicePixelRatio),
			crispCoordinate(boardMargin, devicePixelRatio),
			right - boardMargin,
			bottom - boardMargin));

		// Column dividers
		for (int col = 1; col < outputColumns; ++col)
		{
			const qreal x = left + col * cellWidth;
			painter.drawLine(
				QPointF(x, top),
				QPointF(x, bottom));
		}

		// Row dividers
		for (int row = 1; row < pathRowCount; ++row)
		{
			const qreal y = top + row * cellHeight;
			painter.drawLine(
				QPointF(boardMargin, y),
				QPointF(right, y));
		}

		// Header and label column divider
		painter.drawLine(
			QPointF(left, boardMargin),
			QPointF(left, bottom));
		painter.drawLine(
			QPointF(boardMargin, top),
			QPointF(right, top));

		// Draw labels and headers
		QFont labelFont = matrixMonoFont();
		const int fontSize = std::max(7, labelFont.pointSize() - 1);
		labelFont.setPointSize(fontSize);
		painter.setFont(labelFont);

		painter.setPen(muted);

		// Row/Column header
		painter.drawText(
			QRectF(
				boardMargin,
				boardMargin,
				boardLabelWidth,
				boardHeaderHeight),
			Qt::AlignCenter,
			tr("R/C"));

		// Column headers
		for (int col = 0; col < outputColumns; ++col)
		{
			painter.drawText(
				QRectF(
					left + col * cellWidth,
					boardMargin,
					cellWidth,
					boardHeaderHeight),
				Qt::AlignCenter,
				tr("O%1").arg(col + 1, 2, 10, QLatin1Char('0')));
		}

		// Row labels (speaker groups and bass paths)
		const int groupCount = nonNegative(cardState.speakerGroupCount);
		const int bassCount = nonNegative(cardState.bassPathCount);

		for (int row = 0; row < pathRowCount; ++row)
		{
			QString label;
			if (row < groupCount)
			{
				label = tr("G%1").arg(row + 1, 2, 10, QLatin1Char('0'));
			}
			else if (row < groupCount + bassCount)
			{
				label = tr("B%1").arg(
					row - groupCount + 1, 2, 10, QLatin1Char('0'));
			}
			else
			{
				label = tr("LFE");
			}

			painter.drawText(
				QRectF(
					boardMargin,
					top + row * cellHeight,
					boardLabelWidth,
					cellHeight),
				Qt::AlignCenter,
				label);
		}

		// Draw active cells
		if (cardState.activeMatrixEdges > 0)
		{
			const int fullOutputCount = physicalOutputCount(cardState);
			const int fullRowCount = groupCount + bassCount
				+ (cardState.sourceLfePreserved ? 1 : 0);
			const int fullCapacity = fullOutputCount * fullRowCount;

			const double density = std::clamp(
				static_cast<double>(cardState.activeMatrixEdges)
					/ static_cast<double>(fullCapacity),
				0.0,
				1.0);

			const int visibleCapacity = outputColumns * pathRowCount;
			const int visibleActiveCount = std::max(1,
				static_cast<int>(std::round(
					density * visibleCapacity)));

			// Deterministic pseudo-random spread
			int stride = 7;
			while (stride < visibleCapacity / 2
				&& std::gcd(stride, visibleCapacity) != 1)
			{
				++stride;
			}

			painter.fillRect(rect(), base);
			painter.setPen(Qt::NoPen);
			painter.setBrush(accent);

			for (int index = 0; index < visibleActiveCount; ++index)
			{
				const int cellIndex =
					(index * stride) % visibleCapacity;
				const int row = cellIndex / outputColumns;
				const int col = cellIndex % outputColumns;

				const QRectF cellRect(
					left + col * cellWidth + 1,
					top + row * cellHeight + 1,
					cellWidth - 1,
					cellHeight - 1);

				if (cellRect.width() > 0 && cellRect.height() > 0)
					painter.drawRect(cellRect);
			}
		}
	}

private:
	void recalculateGeometry()
	{
		const int groupCount = nonNegative(cardState.speakerGroupCount);
		const int bassCount = nonNegative(cardState.bassPathCount);
		const int lfeRow = cardState.sourceLfePreserved ? 1 : 0;

		pathRowCount = groupCount + bassCount + lfeRow;

		const int derivedOutputs = physicalOutputCount(cardState);
		if (derivedOutputs > 16)
		{
			outputColumns = 16;
			inferredOutputCount = true;
		}
		else
		{
			outputColumns = derivedOutputs;
			inferredOutputCount = false;
		}
	}

	int physicalOutputCount(const BassManagementCardState& state) const
	{
		static const QRegularExpression layoutPattern(
			QStringLiteral(R"((\d+)(?:\.(\d+))?(?:\.(\d+))?)"));

		const QRegularExpressionMatch match =
			layoutPattern.match(state.layoutLabel);

		if (match.hasMatch())
		{
			int sum = 0;
			for (int i = 1; i <= 3; ++i)
			{
				const QString captured = match.captured(i);
				if (!captured.isEmpty())
					sum += captured.toInt();
			}
			if (sum > 0)
				return std::min(64, sum);
		}

		return std::max(2, nonNegative(state.speakerGroupCount)
			+ std::max(1, nonNegative(state.bassPathCount)));
	}

	BassManagementCardState cardState;
	int pathRowCount = 1;
	int outputColumns = 2;
	bool inferredOutputCount = false;
};

MatrixBassManagementCardView::MatrixBassManagementCardView(
	QWidget* parent)
	: BassManagementCardView(parent)
{
	setObjectName(QStringLiteral("MatrixBassManagementCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(8);

	summaryStrip = new QWidget(this);
	summaryStrip->setObjectName(QStringLiteral("MatrixBassSummaryStrip"));
	QHBoxLayout* summaryLayout = new QHBoxLayout(summaryStrip);
	summaryLayout->setContentsMargins(0, 0, 0, 0);
	summaryLayout->setSpacing(6);

	validityCell = createReadoutCell(
		QStringLiteral("MatrixBassValidityCell"),
		tr("Bass-management validity state"),
		tr("Valid or invalid state indicator"));
	summaryLayout->addWidget(validityCell);

	layoutCell = createReadoutCell(
		QStringLiteral("MatrixBassLayoutCell"),
		tr("Speaker layout"),
		tr("Physical speaker layout: L/R/C/LS/RS/LFE"));
	summaryLayout->addWidget(layoutCell, 1);

	crossoverCell = createReadoutCell(
		QStringLiteral("MatrixBassCrossoverCell"),
		tr("Representative crossovers"),
		tr("High-pass and low-pass crossover sections"));
	summaryLayout->addWidget(crossoverCell, 1);

	root->addWidget(summaryStrip);

	board = new MatrixBassManagementBoard(this);
	root->addWidget(board);

	coordinateLine = new QLabel(this);
	coordinateLine->setObjectName(
		QStringLiteral("MatrixBassCoordinateLine"));
	coordinateLine->setFont(matrixMonoFont());
	coordinateLine->setWordWrap(true);
	coordinateLine->setAccessibleName(
		tr("Coordinate and grid information"));
	root->addWidget(coordinateLine);

	secondaryStrip = new QWidget(this);
	secondaryStrip->setObjectName(
		QStringLiteral("MatrixBassSecondaryStrip"));
	QHBoxLayout* secondaryLayout = new QHBoxLayout(secondaryStrip);
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
		tr("Automatic or manual headroom trim"));
	secondaryLayout->addWidget(headroomCell, 1);

	profileCell = createReadoutCell(
		QStringLiteral("MatrixBassProfileCell"),
		tr("Bass-management profile"),
		tr("Embedded profile or linked name"));
	secondaryLayout->addWidget(profileCell, 1);

	root->addWidget(secondaryStrip);

	statusLine = new QLabel(this);
	statusLine->setObjectName(
		QStringLiteral("MatrixBassStatusLine"));
	statusLine->setFont(matrixMonoFont());
	statusLine->setWordWrap(true);
	statusLine->setVisible(false);
	statusLine->setAccessibleName(
		tr("Status messages and alerts"));
	root->addWidget(statusLine);

	actionRow = new QWidget(this);
	actionRow->setObjectName(
		QStringLiteral("MatrixBassActionRow"));
	actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	actionLayout->addStretch();
	root->addWidget(actionRow);

	connect(
		SkinManager::instance(),
		&SkinManager::skinChanged,
		this,
		[this]()
	{
		board->update();
		update();
	});

	applyState(state());
}

QLabel* MatrixBassManagementCardView::createReadoutCell(
	const QString& objectName,
	const QString& accessibleName,
	const QString& toolTip)
{
	QLabel* cell = new QLabel(this);
	cell->setObjectName(objectName);
	cell->setAttribute(Qt::WA_StyledBackground, true);
	cell->setFont(matrixMonoFont());
	cell->setWordWrap(true);
	cell->setMinimumWidth(0);
	cell->setAccessibleName(accessibleName);
	cell->setToolTip(toolTip);
	return cell;
}

void MatrixBassManagementCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setMinimumSize(40, 40);
	actionLayout->addWidget(button);
}

void MatrixBassManagementCardView::applyState(
	const BassManagementCardState& state)
{
	validityCell->setText(state.valid
		? tr("STATE | VALID")
		: tr("STATE | INVALID"));
	validityCell->setProperty("valid", state.valid);

	layoutCell->setText(state.layoutLabel.isEmpty()
		? tr("LAYOUT | -")
		: tr("LAYOUT | %1").arg(state.layoutLabel));

	QStringList crossovers;
	if (!state.representativeHighPass.isEmpty())
		crossovers.append(tr("HP %1").arg(state.representativeHighPass));
	if (!state.representativeLowPass.isEmpty())
		crossovers.append(tr("LP %1").arg(state.representativeLowPass));

	crossoverCell->setText(
		crossovers.isEmpty()
			? tr("XOVER | -")
			: tr("XOVER | %1").arg(crossovers.join(tr(" / "))));

	pathsCell->setText(
		tr("%1 GRP | %2 BASS | %3 XPT")
			.arg(std::max(0, state.speakerGroupCount))
			.arg(std::max(0, state.bassPathCount))
			.arg(std::max(0, state.activeMatrixEdges)));

	sourceLfeCell->setText(state.sourceLfePreserved
		? tr("LFE | %1 dB").arg(
			QString::number(state.sourceLfeGainDb, 'f', 1))
		: tr("LFE | OFF"));

	if (std::isfinite(state.headroomTrimDb))
	{
		headroomCell->setText(state.headroomAuto
			? tr("HDROOM | AUTO %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1))
			: tr("HDROOM | MANUAL %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1)));
	}
	else
	{
		headroomCell->setText(state.headroomAuto
			? tr("HDROOM | AUTO N/A")
			: tr("HDROOM | MANUAL N/A"));
	}

	if (state.linkedProfile)
	{
		profileCell->setText(state.profileName.isEmpty()
			? tr("PROFILE | LINKED")
			: tr("PROFILE | LINKED %1").arg(
				state.profileName));
	}
	else
	{
		profileCell->setText(state.profileName.isEmpty()
			? tr("PROFILE | EMBEDDED")
			: tr("PROFILE | %1").arg(
				state.profileName));
	}

	board->setCardState(state);

	QStringList coordinate;
	coordinate.append(
		tr("GRID: R01-R%1 / O01-O%2")
			.arg(board->rowCount())
			.arg(board->columnCount()));

	if (board->outputCountIsInferred())
	{
		coordinate.append(
			tr("(inferred from layout; full: %1 outputs)")
				.arg(board->fullOutputCount(state)));
	}

	coordinate.append(
		tr("Active routes: %1 / %2 capacity")
			.arg(std::max(0, state.activeMatrixEdges))
			.arg(board->rowCount() * board->columnCount()));

	coordinateLine->setText(coordinate.join(QStringLiteral(" | ")));

	QStringList status;
	if (!state.errorText.isEmpty())
	{
		status.append(tr("X ERROR: %1").arg(state.errorText));
	}
	if (!state.valid)
	{
		status.append(tr("X ERROR: State is invalid"));
	}
	if (state.linkedProfile && state.profileMissing)
	{
		const QString profileName = state.profileName.isEmpty()
			? tr("unnamed")
			: state.profileName;
		status.append(
			tr("X ERROR: Linked profile \"%1\" is missing")
				.arg(profileName));
	}
	if (!state.warningText.isEmpty())
	{
		status.append(
			tr("! WARNING: %1").arg(state.warningText));
	}

	statusLine->setText(status.join(QLatin1Char('\n')));
	statusLine->setVisible(!status.isEmpty());
	statusLine->setToolTip(status.join(QLatin1Char('\n')));

	updateResponsiveVisibility();
	update();
}

void MatrixBassManagementCardView::paintEvent(
	QPaintEvent* event)
{
	BassManagementCardView::paintEvent(event);

	if (!hasFocus())
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	const qreal devicePixelRatio =
		painter.device()->devicePixelRatioF();
	painter.setPen(crispPen(
		palette().color(QPalette::Highlight),
		devicePixelRatio));

	const qreal left = crispCoordinate(1.0, devicePixelRatio);
	const qreal top = crispCoordinate(1.0, devicePixelRatio);
	const qreal right = crispCoordinate(
		width() - 1.0 - 1.0 / devicePixelRatio,
		devicePixelRatio);
	const qreal bottom = crispCoordinate(
		height() - 1.0 - 1.0 / devicePixelRatio,
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

void MatrixBassManagementCardView::updateResponsiveVisibility()
{
	const int availableWidth = width();

	secondaryStrip->setVisible(
		availableWidth >= secondaryFoldWidth);
	board->setVisible(availableWidth >= narrowWidth);
	coordinateLine->setVisible(availableWidth >= narrowWidth);
}
