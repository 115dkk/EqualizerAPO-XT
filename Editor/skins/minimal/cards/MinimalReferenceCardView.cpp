#include "MinimalReferenceCardView.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QStyle>
#include <QToolButton>

#include "Editor/widgets/ElidedLabel.h"

namespace
{
// Re-evaluate one child's QSS after one of its dynamic properties changed.
// The base class repolishes only the view itself after setState, which does
// not re-resolve property-dependent rules on descendants.
void repolishChild(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

// The engraved command token for a host action: uppercase mono words are the
// terminal's grammar, so they stay untranslated; the host's tooltips carry
// the translated explanation. Browse's LOCATE recovery swap happens in
// applyState because it depends on the state.
QString commandToken(ReferenceCardView::ActionRole role)
{
	switch (role)
	{
	case ReferenceCardView::ActionRole::Browse:
		return QStringLiteral("BROWSE");
	case ReferenceCardView::ActionRole::OpenTarget:
		return QStringLiteral("OPEN");
	case ReferenceCardView::ActionRole::Import:
		return QStringLiteral("IMPORT");
	case ReferenceCardView::ActionRole::OpenPanel:
		return QStringLiteral("PANEL");
	case ReferenceCardView::ActionRole::Options:
		return QStringLiteral("OPT");
	case ReferenceCardView::ActionRole::EditPath:
		return QStringLiteral("EDIT");
	}
	return QString();
}
}

MinimalReferenceCardView::MinimalReferenceCardView(const QString& kind, QWidget* parent)
	: ReferenceCardView(parent)
{
	// The state's kind (mirrored as the refKind property by the base) is all
	// the differentiation this skin wants: Include, Convolution and VST are
	// deliberately the same one-line grammar (constitution).
	Q_UNUSED(kind);

	QWidget* page = contentWidget();
	lineLayout = new QHBoxLayout(page);
	lineLayout->setContentsMargins(0, 0, 0, 0);
	lineLayout->setSpacing(10);

	// The path prints container-first, the way a terminal prints one: the
	// as-written location prefix ("Surround\") in muted ink, then the payload
	// as the line's brightest ink, adjacent - one path, two inks. A folder
	// printed after (or under) the file would invert the containment.
	QHBoxLayout* pathLayout = new QHBoxLayout();
	pathLayout->setContentsMargins(0, 0, 0, 0);
	pathLayout->setSpacing(0);

	// Location prefix: the least defended column on the line - it shrinks
	// (and paint-time elides) first because its full text survives in the
	// tooltip.
	dirLabel = new ElidedLabel(page);
	dirLabel->setObjectName(QStringLiteral("MinimalRefDir"));
	dirLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	dirLabel->setVisible(false);
	pathLayout->addWidget(dirLabel, 0, Qt::AlignVCenter);

	// Payload: the line's data and its brightest ink. Maximum size policy
	// packs it at its natural width and lets the layout compress it when the
	// line runs out of columns; ElidedLabel then middle-elides at paint time
	// (keeping the extension tail) and carries the full text as tooltip.
	nameLabel = new ElidedLabel(page);
	nameLabel->setObjectName(QStringLiteral("MinimalRefName"));
	nameLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	installNameActivation(nameLabel);
	pathLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

	lineLayout->addLayout(pathLayout);

	// The broken-reference marker: an inverted block token (fg/bg swap).
	missingToken = new QLabel(QStringLiteral("MISSING"), page);
	missingToken->setObjectName(QStringLiteral("MinimalRefMissing"));
	missingToken->setAttribute(Qt::WA_StyledBackground, true);
	missingToken->setVisible(false);
	lineLayout->addWidget(missingToken, 0, Qt::AlignVCenter);

	// Bare mono tokens: the format (VST2/VST3) and the ABS portability
	// marker. Print, not pills.
	formatToken = new QLabel(page);
	formatToken->setObjectName(QStringLiteral("MinimalRefFormat"));
	formatToken->setVisible(false);
	lineLayout->addWidget(formatToken, 0, Qt::AlignVCenter);

	absToken = new QLabel(QStringLiteral("ABS"), page);
	absToken->setObjectName(QStringLiteral("MinimalRefAbs"));
	absToken->setVisible(false);
	lineLayout->addWidget(absToken, 0, Qt::AlignVCenter);

	// Measured facts in muted mono. Numbers never elide: a truncated figure
	// is misinformation, so this stays a plain label with its natural width.
	readoutLabel = new QLabel(page);
	readoutLabel->setObjectName(QStringLiteral("MinimalRefReadout"));
	readoutLabel->setVisible(false);
	lineLayout->addWidget(readoutLabel, 0, Qt::AlignVCenter);

	// Host status folded into the line as a "!"-prefixed diagnostic. Tail
	// elide keeps the marker and the message head; the tooltip has the rest.
	statusLabel = new ElidedLabel(page);
	statusLabel->setObjectName(QStringLiteral("MinimalRefStatus"));
	statusLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	statusLabel->setElideMode(Qt::ElideRight);
	statusLabel->setVisible(false);
	lineLayout->addWidget(statusLabel, 0, Qt::AlignVCenter);

	// Slack collector: the commands sit at the line's end, everything else
	// packs to the left like a printed line.
	lineLayout->addStretch(1);
}

void MinimalReferenceCardView::placeActionButton(ActionRole role, QAbstractButton* button)
{
	button->setParent(contentWidget());
	// Re-engrave the host's pictogram button as a terminal command word: in
	// this skin a glyph must carry information, and these words do it better.
	// Text and icon are presentation (the view's), while behavior, enabled
	// state, visibility and the translated tooltip remain the host's.
	button->setProperty("minimalRefCommand", true);
	button->setText(commandToken(role));
	button->setIcon(QIcon());
	QToolButton* toolButton = qobject_cast<QToolButton*>(button);
	if (toolButton != nullptr)
		toolButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	// Before the slack collector: the line still ends with the commands,
	// but packed to the left with the rest of the print instead of flushed
	// to the terminal's right edge.
	lineLayout->insertWidget(lineLayout->count() - 1, button, 0, Qt::AlignVCenter);
	repolishChild(button);
}

void MinimalReferenceCardView::placeBusStrip(QWidget* strip)
{
	// The line reads on: path, tokens, readout, then the bus expression,
	// still one printed line - data first, diagnostics after, commands at
	// the end. The strip paints its own terminal grammar.
	strip->setParent(contentWidget());
	lineLayout->insertWidget(lineLayout->indexOf(statusLabel), strip, 0, Qt::AlignVCenter);
}

void MinimalReferenceCardView::addLeadingWidget(QWidget* widget)
{
	widget->setParent(contentWidget());
	// Part of the reference grammar ("<channel> <file>"), so it leads the
	// line. The property turns it into this skin's selector idiom: a caption
	// wearing a caret - transparent on the strip, 1px underline, accent only
	// while open/focused (see precision_*.qss).
	widget->setProperty("minimalRefSelector", true);
	lineLayout->insertWidget(leadingIndex, widget, 0, Qt::AlignVCenter);
	++leadingIndex;
	repolishChild(widget);
}

void MinimalReferenceCardView::applyState(const ReferenceCardState& state)
{
	nameLabel->setFullText(state.name);
	if (!state.fullPath.isEmpty())
		nameLabel->setToolTip(state.fullPath);
	// While the reference is broken the payload's information is not there,
	// so its ink drops to secondary and the inverted MISSING block carries
	// the emphasis - state as ink, not decoration.
	nameLabel->setProperty("refMissing", state.missing);
	repolishChild(nameLabel);

	missingToken->setVisible(state.missing);

	formatToken->setVisible(!state.formatBadge.isEmpty());
	formatToken->setText(state.formatBadge);

	absToken->setVisible(state.absolutePath && !state.missing);

	dirLabel->setVisible(!state.directory.isEmpty());
	dirLabel->setFullText(state.locationPrefix());

	readoutLabel->setVisible(!state.readout.isEmpty());
	// Two-space runs are the terminal's column separator; the middle dot is
	// reserved for "no meaning" in this skin.
	readoutLabel->setText(state.readout.join(QStringLiteral("  ")));

	const bool hasStatus = !state.statusText.isEmpty();
	statusLabel->setVisible(hasStatus);
	if (hasStatus)
	{
		// Diagnostic grammar: "!" warns, "!!" is critical. The marker states
		// the severity in pure text; the ink tag (QSS severity property)
		// repeats it in colour, like the analysis chips.
		const QString marker = state.statusSeverity == ReferenceCardState::Severity::Critical
			? QStringLiteral("!!") : QStringLiteral("!");
		statusLabel->setFullText(marker + QLatin1Char(' ') + state.statusText);
	}
	statusLabel->setProperty("severity", referenceCardSeverityName(state.statusSeverity));
	repolishChild(statusLabel);

	// The Browse command doubles as the Locate recovery entry while the
	// reference is broken. Same condition the hosts use for their translated
	// "Locate..." label; the token is the terminal's word for it, the host's
	// tooltip keeps the translated explanation.
	if (QAbstractButton* browse = actionButton(ActionRole::Browse))
		browse->setText(locateMode()
			? QStringLiteral("LOCATE") : QStringLiteral("BROWSE"));

	// Command words never give way: the sheet declares min-width: 0 for
	// compactness, which lets layout pressure (the VST bus expression is a
	// wide, unshrinkable neighbour) crush the engravings into ellipses. A
	// clipped command carries nothing, while the data columns elide into
	// tooltips - so the floor is restated from each engraved word, after
	// the state pass that may have swapped BROWSE for LOCATE.
	for (ActionRole role : {ActionRole::Browse, ActionRole::OpenTarget, ActionRole::Import,
		ActionRole::OpenPanel, ActionRole::Options, ActionRole::EditPath})
	{
		QAbstractButton* button = actionButton(role);
		if (button != nullptr)
			button->setMinimumWidth(
				button->fontMetrics().horizontalAdvance(button->text()) + 16);
	}
}
