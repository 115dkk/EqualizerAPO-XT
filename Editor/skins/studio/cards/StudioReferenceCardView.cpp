#include "Editor/skins/studio/cards/StudioReferenceCardView.h"

#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/widgets/ElidedLabel.h"

namespace
{
// Re-evaluate a widget's stylesheet after one of its dynamic properties
// changed. The base class only repolishes the view itself, so children whose
// QSS keys off their own properties are refreshed here, deterministically.
void repolishWidget(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

// A chip in the type badge's lit-glass grammar (studio.md: colored ink over
// a translucent same-color fill; an unlit chip is quiet data ink). Role and
// lit-ness are dynamic properties the studio sheets select on.
QLabel* makeChip(const QString& text, const QString& role, QWidget* parent)
{
	QLabel* chip = new QLabel(text, parent);
	chip->setObjectName(QStringLiteral("StudioRefChip"));
	chip->setProperty("chipRole", role);
	chip->setProperty("chipLit", true);
	chip->setAttribute(Qt::WA_StyledBackground, true);
	chip->setVisible(false);
	return chip;
}
}

StudioReferenceCardView::StudioReferenceCardView(const QString& kind, QWidget* parent)
	: ReferenceCardView(parent)
{
	// The base sets refKind at the first setState; setting it here too lets
	// children polish against the kind from the very first show.
	setProperty("refKind", kind);

	QWidget* page = contentWidget();
	QVBoxLayout* root = new QVBoxLayout(page);
	// The left inset gives the identity type its margin - print pressed
	// against the card's edge reads as cramped, not calm.
	root->setContentsMargins(8, 0, 0, 0);
	root->setSpacing(5);

	// Identity line: the name carries the luminance hierarchy (brightest ink
	// on the pane), chips follow it as data, actions rest at the right edge.
	// No icon - if it is not a label or a value, it is not on the glass.
	identityLayout = new QHBoxLayout();
	identityLayout->setContentsMargins(0, 0, 0, 0);
	identityLayout->setSpacing(8);

	nameLabel = new ElidedLabel(page);
	nameLabel->setObjectName(QStringLiteral("StudioRefName"));
	installNameActivation(nameLabel);
	identityLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

	formatChip = makeChip(QString(), QStringLiteral("format"), page);
	identityLayout->addWidget(formatChip, 0, Qt::AlignVCenter);

	absChip = makeChip(QStringLiteral("ABS"), QStringLiteral("abs"), page);
	identityLayout->addWidget(absChip, 0, Qt::AlignVCenter);

	missingChip = makeChip(QStringLiteral("MISSING"), QStringLiteral("missing"), page);
	identityLayout->addWidget(missingChip, 0, Qt::AlignVCenter);

	// The action buttons follow the chips instead of riding the pane's
	// right edge; the stretch owns the leftover identity-line width.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	identityLayout->addLayout(actionLayout);

	identityLayout->addStretch(1);

	root->addLayout(identityLayout);

	// The data window: a sunken glass inset holding the location (left, mono,
	// paint-time middle elide) and the measured facts (right). It is data
	// behind glass - quiet - and exists only while it holds data.
	windowPane = new QWidget(page);
	windowPane->setObjectName(QStringLiteral("StudioRefWindow"));
	windowPane->setAttribute(Qt::WA_StyledBackground, true);
	windowPane->setVisible(false);
	windowLayout = new QHBoxLayout(windowPane);
	// The left margin indents the data one character from the pane's edge -
	// print does not start at the very edge of the page.
	windowLayout->setContentsMargins(18, 3, 10, 3);
	windowLayout->setSpacing(10);

	// The location prints as the containing prefix ("Surround\"): the folder
	// holds the file, so it must read as a path prefix, never as a sub-item
	// hanging off the name.
	locationLabel = new ElidedLabel(windowPane);
	locationLabel->setObjectName(QStringLiteral("StudioRefLocation"));
	windowLayout->addWidget(locationLabel, 1, Qt::AlignVCenter);

	factsLabel = new QLabel(windowPane);
	factsLabel->setObjectName(QStringLiteral("StudioRefFacts"));
	windowLayout->addWidget(factsLabel, 0, Qt::AlignVCenter);

	root->addWidget(windowPane);

	// Status: a small lamp wearing the severity color next to one quiet
	// muted line. The lamp carries the light; the words never shout.
	statusRow = new QWidget(page);
	statusRow->setObjectName(QStringLiteral("StudioRefStatusRow"));
	statusRow->setVisible(false);
	QHBoxLayout* statusLayout = new QHBoxLayout(statusRow);
	statusLayout->setContentsMargins(0, 0, 0, 0);
	statusLayout->setSpacing(6);

	// U+25CF: the lamp dot.
	statusLamp = new QLabel(QString(QChar(0x25CF)), statusRow);
	statusLamp->setObjectName(QStringLiteral("StudioRefLamp"));
	statusLayout->addWidget(statusLamp, 0, Qt::AlignVCenter);

	statusLabel = new QLabel(statusRow);
	statusLabel->setObjectName(QStringLiteral("StudioRefStatus"));
	statusLabel->setWordWrap(true);
	statusLayout->addWidget(statusLabel, 1, Qt::AlignVCenter);

	root->addWidget(statusRow);
}

void StudioReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	Q_UNUSED(role);
	actionLayout->addWidget(button, 0, Qt::AlignVCenter);
	actionButtons.append(button);
}

void StudioReferenceCardView::addLeadingWidget(QWidget* widget)
{
	// The reference grammar reads "<channel> <file>": the selector opens the
	// identity line, ahead of the name.
	widget->setParent(contentWidget());
	identityLayout->insertWidget(0, widget, 0, Qt::AlignVCenter);
}

void StudioReferenceCardView::placeBusStrip(QWidget* strip)
{
	// The bus contract is data about the plugin, so it lives behind the same
	// sunken glass as the location: in the data window, right of the
	// location's stretch. The identity line stays the name's - and the
	// action buttons' - row.
	busStrip = strip;
	strip->setParent(windowPane);
	windowLayout->insertWidget(1, strip, 0, Qt::AlignVCenter);
}

void StudioReferenceCardView::applyState(const ReferenceCardState& state)
{
	// Identity: luminance says whether the target answers. A resolved name
	// is the brightest ink on the pane; a broken reference drops to muted
	// ink - the light moves to the danger chip.
	nameLabel->setFullText(state.name);
	if (!state.fullPath.isEmpty())
		nameLabel->setToolTip(state.fullPath);
	nameLabel->setProperty("refMissing", state.missing);
	nameLabel->setProperty("refClickable", state.nameClickable && !state.missing);
	repolishWidget(nameLabel);

	// One light per row: while the reference is broken the danger chip is
	// the only lit chip; the format chip falls back to unlit data ink.
	formatChip->setText(state.formatBadge);
	formatChip->setVisible(!state.formatBadge.isEmpty());
	formatChip->setProperty("chipLit", !state.missing);
	repolishWidget(formatChip);

	absChip->setVisible(state.absolutePath && !state.missing);
	// An empty reference is unconfigured, not broken: the muted "No file
	// selected" identity says it all, no alarm lamp (deletion first).
	missingChip->setVisible(state.missing && !state.editText.isEmpty());

	// The window is the card's data anchor. Every configured reference has a
	// location fact - the as-written containing prefix, or, for a bare
	// reference, the resolved directory it sits in - and a broken one has the
	// datum to fix: the reference as written when it says more than the name,
	// otherwise the resolved path where the target was expected. Only the
	// unconfigured card keeps a bare pane (nothing is set, so nothing sits
	// behind the glass).
	QString windowText;
	if (state.missing)
	{
		const QString asWritten = state.editText.trimmed();
		if (!asWritten.isEmpty())
			windowText = asWritten != state.name ? asWritten : state.fullPath;
	}
	else if (!state.directory.isEmpty())
	{
		windowText = state.locationPrefix();
	}
	else if (!state.fullPath.isEmpty())
	{
		QString resolvedDir = QDir::toNativeSeparators(QFileInfo(state.fullPath).absolutePath());
		if (!resolvedDir.endsWith(QLatin1Char('\\')) && !resolvedDir.endsWith(QLatin1Char('/')))
			resolvedDir += QDir::separator();
		windowText = resolvedDir;
	}
	locationLabel->setVisible(!windowText.isEmpty());
	locationLabel->setFullText(windowText);
	const bool hasFacts = !state.readout.isEmpty();
	factsLabel->setVisible(hasFacts);
	factsLabel->setText(state.readout.join(QStringLiteral(" %1 ").arg(QChar(0x00B7))));
	// The VST card's bus instrument shares the window; a visible strip keeps
	// the pane up even if the location were ever empty.
	const bool hasBus = busStrip != nullptr && busStrip->isVisibleTo(windowPane);
	windowPane->setVisible(!windowText.isEmpty() || hasFacts || hasBus);

	statusRow->setVisible(!state.statusText.isEmpty());
	statusLabel->setText(state.statusText);
	statusLamp->setProperty("lampSeverity", referenceCardSeverityName(state.statusSeverity));
	repolishWidget(statusLamp);

	// A labelled action shows its words - the host swaps Browse to
	// "Locate..." while the reference is broken - and the Browse button
	// additionally lights an accent border while it acts as the recovery
	// entry (glass that brightens on hover, per the constitution).
	for (QAbstractButton* button : actionButtons)
	{
		QToolButton* toolButton = qobject_cast<QToolButton*>(button);
		if (toolButton != nullptr)
			toolButton->setToolButtonStyle(toolButton->text().isEmpty()
				? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
	}
	if (QAbstractButton* browse = actionButton(ActionRole::Browse))
	{
		browse->setProperty("studioLocate", locateMode());
		repolishWidget(browse);
	}
}
