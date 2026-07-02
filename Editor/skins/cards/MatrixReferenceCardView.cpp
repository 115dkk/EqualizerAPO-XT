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

	// VST = an external-device entry: the port strip heads the body so the
	// plugin reads as outboard gear patched into the signal path. Monochrome
	// furniture under the colour rationing - the ports are muted ink, never
	// accent (accent is reserved for engage/select/hover).
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
		QLabel* device = new QLabel(QStringLiteral("EXTERNAL DEVICE"), portStrip);
		device->setObjectName(QStringLiteral("MatrixRefDeviceLabel"));
		stripLayout->addWidget(device);
		stripLayout->addStretch(1);
		QLabel* outPort = new QLabel(QStringLiteral("OUT >"), portStrip);
		outPort->setObjectName(QStringLiteral("MatrixRefPortLabel"));
		stripLayout->addWidget(outPort);
		root->addWidget(portStrip);
	}

	// The feed line: marker cell + [output bus cell] + payload name + type
	// tokens + location readout, with the action cells on the right.
	QWidget* feedLine = new QWidget(page);
	feedLine->setObjectName(QStringLiteral("MatrixRefFeedLine"));
	feedLayout = new QHBoxLayout(feedLine);
	feedLayout->setContentsMargins(0, 0, 0, 0);
	feedLayout->setSpacing(6);

	markerCell = new QLabel(feedLine);
	markerCell->setObjectName(QStringLiteral("MatrixRefMarker"));
	markerCell->setAttribute(Qt::WA_StyledBackground, true);
	feedLayout->addWidget(markerCell, 0, Qt::AlignVCenter);

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

	locationCell = new ElidedLabel(feedLine);
	locationCell->setObjectName(QStringLiteral("MatrixRefLocation"));
	locationCell->setVisible(false);
	feedLayout->addWidget(locationCell, 1, Qt::AlignVCenter);

	// Keeps the action cells pinned right while the location readout is
	// hidden (an empty location collapses its stretch).
	feedLayout->addStretch(1);

	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	feedLayout->addLayout(actionLayout);

	root->addWidget(feedLine);

	// Readout strip: every measured fact in its own boxed sunken mono cell
	// (rule 5: authoritative numbers live in boxed cells) - the knob value
	// cell's grammar, applied to the impulse-response readout.
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

void MatrixReferenceCardView::addActionButton(ActionRole role, QAbstractButton* button)
{
	// The Browse cell is remembered so applyState can re-speak the host's
	// Locate affordance as a board token; placement is uniform (the host
	// hands the buttons over in display order).
	if (role == ActionRole::Browse)
		browseButton = button;
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
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
	// readout while it does not (danger is the documented ink for a broken
	// include readout - colour rationing, M3). An empty reference has no
	// feed patched (NO FEED); a written one that fails to resolve is a lost
	// feed (MISSING).
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

	// Format code (VST2/VST3): type identity stays monochrome (the
	// typeBadgeStyle precedent).
	formatCell->setVisible(!state.formatBadge.isEmpty());
	formatCell->setText(state.formatBadge);

	// Location readout: muted mono, elided at paint time.
	locationCell->setVisible(!state.directory.isEmpty());
	if (!state.directory.isEmpty())
	{
		locationCell->setFullText(QStringLiteral("@ ") + state.directory);
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
	statusLine->setProperty("severity",
		state.statusSeverity == ReferenceCardState::Severity::Critical ? QStringLiteral("critical")
		: state.statusSeverity == ReferenceCardState::Severity::Warning ? QStringLiteral("warning")
		: QStringLiteral("none"));
	repolishCell(statusLine);

	// Browse doubles as the recovery entry while the reference is broken
	// (AR2 X-4). The host's translated "Locate..." text is re-spoken as the
	// board's untranslated LOCATE token (mono caps); the host's translated
	// tooltip stays. The cell is monochrome at rest - accent arrives only on
	// hover, the engage pre-light of a recovery crosspoint.
	if (browseButton != nullptr)
	{
		const bool locate = state.missing && !state.editText.trimmed().isEmpty();
		browseButton->setText(locate ? QStringLiteral("LOCATE") : QString());
		browseButton->setProperty("matrixLocate", locate);
		if (QToolButton* toolButton = qobject_cast<QToolButton*>(browseButton))
			toolButton->setToolButtonStyle(locate
				? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
		repolishCell(browseButton);
	}
}
