#include "MatrixReferenceCardView.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/widgets/ElidedLabel.h"

namespace
{
// Re-evaluate a widget's stylesheet after one of its dynamic properties
// changed. applyState repolishes the affected children itself: the base class
// only repolishes the view, and rules keyed on a child's own property need
// the child repolished.
void repolishCell(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

// Board designation of a feed kind: the untranslated mono token the marker
// cell posts while the reference resolves. ASCII ">" - DM Mono has no glyph
// for U+25B8 and the offscreen platform renders tofu.
QString feedDesignation(const QString& kind)
{
	if (kind == QStringLiteral("include"))
		return QStringLiteral("> SRC");
	if (kind == QStringLiteral("convolution"))
		return QStringLiteral("> IR");
	if (kind == QStringLiteral("multiconvolution"))
		return QStringLiteral("> IR+");
	return QStringLiteral("> DEV");
}
}

MatrixReferenceCardView::MatrixReferenceCardView(const QString& kind, QWidget* parent)
	: ReferenceCardView(parent), cardKind(kind)
{
	QWidget* page = contentWidget();
	QVBoxLayout* root = new QVBoxLayout(page);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(4);

	// VST: the port strip heads the body. Monochrome furniture under the
	// colour rationing - the ports are muted ink, never accent.
	if (cardKind == QStringLiteral("vst"))
	{
		QWidget* portStrip = new QWidget(page);
		portStrip->setObjectName(QStringLiteral("MatrixRefPortStrip"));
		portStrip->setAttribute(Qt::WA_StyledBackground, true);
		QHBoxLayout* stripLayout = new QHBoxLayout(portStrip);
		stripLayout->setContentsMargins(0, 0, 0, 3);
		stripLayout->setSpacing(8);
		QLabel* inPort = new QLabel(QStringLiteral("> IN"), portStrip);
		inPort->setObjectName(QStringLiteral("MatrixRefPortLabel"));
		stripLayout->addWidget(inPort);
		stripLayout->addStretch(1);
		deviceLabel = new QLabel(QStringLiteral("EXTERNAL DEVICE"), portStrip);
		deviceLabel->setObjectName(QStringLiteral("MatrixRefDeviceLabel"));
		stripLayout->addWidget(deviceLabel);
		stripLayout->addStretch(1);
		QLabel* outPort = new QLabel(QStringLiteral("OUT >"), portStrip);
		outPort->setObjectName(QStringLiteral("MatrixRefPortLabel"));
		stripLayout->addWidget(outPort);
		root->addWidget(portStrip);
	}

	// The feed line: marker cell + [output bus cell] + location readout +
	// payload name + type tokens, with the action cells on the right. The
	// location leads the payload in reading order ("Surround@ example.txt") -
	// a location after the name would read as the name's appendix.
	QWidget* feedLine = new QWidget(page);
	feedLine->setObjectName(QStringLiteral("MatrixRefFeedLine"));
	feedLayout = new QHBoxLayout(feedLine);
	feedLayout->setContentsMargins(0, 0, 0, 0);
	feedLayout->setSpacing(6);

	markerCell = new QLabel(feedLine);
	markerCell->setObjectName(QStringLiteral("MatrixRefMarker"));
	markerCell->setAttribute(Qt::WA_StyledBackground, true);
	feedLayout->addWidget(markerCell, 0, Qt::AlignVCenter);

	locationCell = new ElidedLabel(feedLine);
	locationCell->setObjectName(QStringLiteral("MatrixRefLocation"));
	locationCell->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	locationCell->setVisible(false);
	feedLayout->addWidget(locationCell, 0, Qt::AlignVCenter);

	nameCell = new ElidedLabel(feedLine);
	nameCell->setObjectName(QStringLiteral("MatrixRefName"));
	installNameActivation(nameCell);
	feedLayout->addWidget(nameCell, 0, Qt::AlignVCenter);

	absCell = new QLabel(QStringLiteral("ABS"), feedLine);
	absCell->setObjectName(QStringLiteral("MatrixRefAbsCell"));
	absCell->setAttribute(Qt::WA_StyledBackground, true);
	absCell->setVisible(false);
	feedLayout->addWidget(absCell, 0, Qt::AlignVCenter);

	formatCell = new QLabel(feedLine);
	formatCell->setObjectName(QStringLiteral("MatrixRefFormatCell"));
	formatCell->setAttribute(Qt::WA_StyledBackground, true);
	formatCell->setVisible(false);
	feedLayout->addWidget(formatCell, 0, Qt::AlignVCenter);

	// The action cells follow the codes on the feed line; the stretch
	// owns the line's tail instead of pushing them to the board's edge.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	feedLayout->addLayout(actionLayout);

	feedLayout->addStretch(1);

	root->addWidget(feedLine);

	// Readout strip: every measured fact in its own boxed sunken mono cell
	// (matrix.md rule 5: authoritative numbers live in boxed cells).
	readoutStrip = new QWidget(page);
	readoutStrip->setObjectName(QStringLiteral("MatrixRefReadoutStrip"));
	readoutLayout = new QHBoxLayout(readoutStrip);
	readoutLayout->setContentsMargins(0, 0, 0, 0);
	readoutLayout->setSpacing(4);
	readoutLayout->addStretch(1);
	readoutStrip->setVisible(false);
	root->addWidget(readoutStrip);

	statusLine = new QLabel(page);
	statusLine->setObjectName(QStringLiteral("MatrixRefStatusLine"));
	// A non-wrapping QLabel's minimum width is its text width; a long
	// translated status must wrap instead of widening the row (960px gate).
	statusLine->setWordWrap(true);
	statusLine->setVisible(false);
	root->addWidget(statusLine);
}

void MatrixReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	// The Browse cell is remembered so applyState can re-speak the host's
	// Locate affordance as a board token; placement is uniform (the host
	// hands the buttons over in display order).
	Q_UNUSED(role);
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
}

void MatrixReferenceCardView::placeBusStrip(QWidget* strip)
{
	// The bus cells join the feed line after the type code: reading order
	// stays marker, place, payload, codes, then the port formats, then the
	// action cells; the stretch owns the tail of the line.
	strip->setParent(contentWidget());
	feedLayout->insertWidget(feedLayout->indexOf(formatCell) + 1, strip, 0, Qt::AlignVCenter);
}

void MatrixReferenceCardView::addLeadingWidget(QWidget* widget)
{
	// The MultiConvolution output-channel select: the output bus designation,
	// dressed by QSS as a sunken mono coordinate cell right after the feed
	// marker ("<channel> <file>" - the reference grammar keeps its word
	// order).
	widget->setProperty("matrixBusCell", true);
	feedLayout->insertWidget(1, widget, 0, Qt::AlignVCenter);
}

void MatrixReferenceCardView::applyState(const ReferenceCardState& state)
{
	// Feed marker: the board designation while the feed resolves; the danger
	// readout while it does not. An empty reference has no feed patched
	// (NO FEED); a written one that fails to resolve is a lost feed
	// (MISSING).
	if (state.missing)
		markerCell->setText(state.editText.trimmed().isEmpty()
			? QStringLiteral("NO FEED") : QStringLiteral("MISSING"));
	else
		markerCell->setText(feedDesignation(cardKind));
	markerCell->setProperty("feedState",
		state.missing ? QStringLiteral("missing") : QStringLiteral("live"));
	repolishCell(markerCell);

	// Payload: the name is data - brightest mono ink, never coloured. Accent
	// appears only as the hover pre-light of the click affordance.
	nameCell->setFullText(state.name);
	nameCell->setToolTip(state.fullPath.isEmpty() ? state.name : state.fullPath);
	nameCell->setProperty("nameClickable", state.nameClickable && !state.missing);
	repolishCell(nameCell);

	// ABS: a hollow amber token - an absolute reference is a portability
	// hazard, and amber is the rationed caution ink (hollow, per the bypass
	// grammar: a hazard notice, not an engaged state).
	absCell->setVisible(state.absolutePath && !state.missing);

	// Format code (VST2/VST3): on a VST feed the loaded ABI is posted in the
	// port strip's device engraving ("EXTERNAL DEVICE · VST3") - a second
	// code cell down on the feed line restated it and nagged the eye
	// (maintainer judgement, r3). Type identity stays monochrome either way
	// (the typeBadgeStyle precedent).
	if (deviceLabel != nullptr)
	{
		formatCell->setVisible(false);
		deviceLabel->setText(state.formatBadge.isEmpty()
			? QStringLiteral("EXTERNAL DEVICE")
			: QStringLiteral("EXTERNAL DEVICE %1 %2")
				.arg(QChar(0x00B7)).arg(state.formatBadge));
	}
	else
	{
		formatCell->setVisible(!state.formatBadge.isEmpty());
		formatCell->setText(state.formatBadge);
	}

	// Location readout: muted mono ahead of the payload, "Surround@" - the
	// at-sign closes the place. Elided at paint time.
	locationCell->setVisible(!state.directory.isEmpty());
	if (!state.directory.isEmpty())
	{
		locationCell->setFullText(state.directory + QStringLiteral("@"));
		locationCell->setToolTip(state.directory);
	}

	// Boxed readout cells, rebuilt per state (the list is tiny); inserted
	// ahead of the trailing stretch so the strip stays left-packed.
	qDeleteAll(readoutCells);
	readoutCells.clear();
	for (const QString& item : state.readout)
	{
		QLabel* cell = new QLabel(item, readoutStrip);
		cell->setObjectName(QStringLiteral("MatrixRefReadoutCell"));
		cell->setAttribute(Qt::WA_StyledBackground, true);
		readoutLayout->insertWidget(readoutLayout->count() - 1, cell, 0, Qt::AlignVCenter);
		readoutCells.append(cell);
	}
	readoutStrip->setVisible(!state.readout.isEmpty());

	// Status: one mono board line under a "!" remark marker; severity speaks
	// through ink alone (amber = caution, danger = error).
	statusLine->setVisible(!state.statusText.isEmpty());
	statusLine->setText(state.statusText.isEmpty()
		? QString() : QStringLiteral("! ") + state.statusText);
	statusLine->setProperty("severity", referenceCardSeverityName(state.statusSeverity));
	repolishCell(statusLine);

	// Browse doubles as the recovery entry while the reference is broken.
	// The host's translated "Locate..." text is re-spoken as the board's
	// untranslated LOCATE token (mono caps); the host's translated tooltip
	// stays. The cell is monochrome at rest - accent arrives only on hover.
	if (QAbstractButton* browse = actionButton(ActionRole::Browse))
	{
		const bool locate = locateMode();
		browse->setText(locate ? QStringLiteral("LOCATE") : QString());
		browse->setProperty("matrixLocate", locate);
		if (QToolButton* toolButton = qobject_cast<QToolButton*>(browse))
			toolButton->setToolButtonStyle(locate
				? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
		repolishCell(browse);
	}
}
