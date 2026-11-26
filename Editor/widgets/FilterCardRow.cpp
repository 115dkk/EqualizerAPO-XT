/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FilterCardRow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include "Editor/widgets/routing/RoutingViewScroll.h"
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#include "Editor/FilterTemplate.h"
#include "Editor/IFilterGUI.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/ButtonName.h"
#include "Editor/widgets/ChBadge.h"
#include "Editor/widgets/CommandRowFacts.h"
#include "Editor/widgets/routing/ChannelIdentity.h"
#include "Editor/widgets/ElidedLabel.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "filters/MultiConvolutionCommand.h"

// The power toggle's glyph: the active skin's, or the shared one.
static QIcon powerToggleIcon(bool enabled)
{
	const QIcon skinned = SkinManager::instance()->powerToggleIcon(enabled);
	if (!skinned.isNull())
		return skinned;
	return QIcon(enabled ? QStringLiteral(":/icons/power_on.svg") : QStringLiteral(":/icons/power_off.svg"));
}


FilterCardRow::FilterCardRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui,
	FilterCardDescriptor preparedDescriptor, QWidget* parent)
	: QWidget(parent), table(table), item(item), gui(gui), descriptor(std::move(preparedDescriptor)), rowNumber(number)
{
	setAttribute(Qt::WA_StyledBackground, false);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

	// The layout differs from the first widget on, so the table's mode is
	// read before anything is built. In ModernRows every line is a row; a
	// blank line stays the thin spacer it is in a card list.
	inlineRow = table != nullptr && table->effectiveRenderMode() == FilterTable::ModernRows
		&& descriptor.kind != FilterCardKind::Spacer;
	// Whether the row mounts an editor (its own GUI or the skin's Copy
	// routing view) or is a body-less line whose content is its title and
	// summary. The same test mountBody makes.
	hasEditor = gui != nullptr || (FilterCardModel::opensRoutingView(descriptor)
		&& SkinManager::instance()->routingRenderer() != nullptr);

	QVBoxLayout* cardLayout = buildCardFrame();

	// Dress the frame and header now, while the card is two widgets deep:
	// setStyleSheet() re-resolves the style of every descendant, so applying
	// these sheets to the finished ~40-widget card (the applyDescriptor() call
	// at the end of this constructor used to be the first apply) re-styled the
	// whole subtree per card - measured ~3 ms each on a 300-row config.
	// Children created below inherit the sheets as they appear, and the
	// applyDescriptor() re-run finds identical strings and properties, so it
	// skips both the re-set and the construction-time repolish entirely.
	syncVisualState();

	QHBoxLayout* headerLayout = buildHeaderControls(number);
	buildHeaderIdentity(headerLayout);
	buildBodyStack(cardLayout);
	mountBody();

	bodyStack->setVisible(expanded);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		syncVisualState();
		update();
	});
	// Per-command-type chrome hook: rows are recreated on every skin switch
	// (FilterTable::updateGuis), so construction time is the right moment for
	// the active skin to tag or extend this row.
	SkinManager::instance()->prepareCommandRow(currentRowInfo(), cardFrame, headerWidget, bodyStack);
	applyDescriptor();

	// Mouse events from the header's blank/label surface naturally bubble to
	// FilterTable, which also owns drag selection. Interactive controls accept
	// their events, though, so a click in an expanded card used to leave a
	// different row focused and could strand a multi-selection indefinitely.
	// Observe only the non-drag surfaces and select their owning card before
	// the control handles the same plain click.
	watchPointerSelection(bodyStack);
	// A row's raw line editor is not inside the stack; a press in it selects
	// the row the same way.
	if (inlineRow)
		watchPointerSelection(lineEdit);
	for (QAbstractButton* button : headerWidget->findChildren<QAbstractButton*>())
		watchPointerSelection(button);
}

QVBoxLayout* FilterCardRow::buildCardFrame()
{
	QVBoxLayout* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(CommandRowInfo::cardMarginLeft + rowIndentUnits() * SkinManager::instance()->tokens().channelGroupIndent,
		CommandRowInfo::cardMarginTop, 8, 4);
	outerLayout->setSpacing(0);
	// Detach the layout's minimumSize from the children's: the body can contain
	// legacy filter GUIs with huge content-driven sizeHints (DeviceFilterGUI,
	// VST, Convolution, ...) and SetMinimumSize would propagate that up here
	// and force every cell in FilterTable's grid to that width.
	outerLayout->setSizeConstraint(QLayout::SetNoConstraint);

	cardFrame = new CommandRowFrame(this);
	cardFrame->setObjectName(QStringLiteral("FilterCardRow"));
	cardFrame->setAttribute(Qt::WA_StyledBackground, true);
	cardFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	outerLayout->addWidget(cardFrame);

	QVBoxLayout* cardLayout = new QVBoxLayout(cardFrame);
	cardLayout->setContentsMargins(0, 0, 0, 0);
	cardLayout->setSpacing(0);

	headerWidget = new QWidget(cardFrame);
	headerWidget->setObjectName(QStringLiteral("FilterCardHeader"));
	headerWidget->setAttribute(Qt::WA_StyledBackground, true);
	headerWidget->setMinimumHeight(SkinManager::instance()->tokens().rowHeight);
	if (inlineRow)
	{
		// A row: the header becomes the left column and the editor or content
		// sits beside it, so the frame lays its children out across instead
		// of down. The property lets a skin sheet size the column's controls.
		cardLayout->setDirection(QBoxLayout::LeftToRight);
		cardFrame->setProperty("inlineRow", true);
		headerWidget->setProperty("inlineRow", true);
		headerWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
		// The column's controls stand in a top band as tall as a one-line
		// row, so number and power keep one distance from the row's top edge
		// whether the editor beside them is one line, many lines or none (a
		// body-less line is one band tall). A Copy row centres the band on
		// the row instead (centresRowMarkers).
		headerWidget->setFixedHeight(InlineBandHeight);
		cardLayout->addWidget(headerWidget, 0, centresRowMarkers() ? Qt::AlignVCenter : Qt::AlignTop);
		return cardLayout;
	}
	cardLayout->addWidget(headerWidget);
	return cardLayout;
}

QHBoxLayout* FilterCardRow::buildHeaderControls(int number)
{
	// 2px vertical margins, not 4: the header's height floor is the skin's
	// rowHeight token, and the slimmer margins let the compact skins actually
	// reach it instead of being held up by the button train.
	QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
	headerLayout->setContentsMargins(8, 2, 8, 2);
	// A one-line row gives every pixel it can to the editor beside it.
	headerLayout->setSpacing(inlineRow ? 6 : 8);

	// Not checkable: an open card is where a card rests, not something
	// switched on, and a checked cap lights in the accent in four skins. That
	// put a lit button on every open card. The glyph already says open or
	// closed.
	expandButton = new QToolButton(headerWidget);
	expandButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	// The header's actions (fold, add, remove, raw edit) rest as bare glyphs
	// in every skin's sheet and take their cap only under hover, press or
	// engagement, so the power switch is the one capped control on a card
	// head (#456: it was told apart from + and the bin by its icon alone).
	expandButton->setProperty("headerAction", true);
	expanded = gui != nullptr;
	expandButton->setText(expanded ? QStringLiteral("v") : QStringLiteral(">"));
	connect(expandButton, &QToolButton::clicked, this, [this]() { setExpanded(!expanded); });
	// A one-line row has no body below to fold. The button still exists,
	// hidden, because setExpanded and the tooltips keep it current.
	if (inlineRow)
		expandButton->setVisible(false);
	else
		headerLayout->addWidget(expandButton);

	numberLabel = new QLabel(QString::number(number), headerWidget);
	numberLabel->setObjectName(QStringLiteral("FilterCardNumber"));
	numberLabel->setAlignment(Qt::AlignCenter);
	// 24 px still holds a three-digit line number in the row's mono face.
	numberLabel->setMinimumWidth(inlineRow ? 24 : 28);
	headerLayout->addWidget(numberLabel);

	// The controls form a fixed column right after the number, the way the
	// original rows laid them out (toolbar and power switch at the left,
	// content after), so every row's switch and buttons line up and the eye
	// never has to travel to the far edge of a wide window to reach them.
	// Field report from a 27-inch screen: content at the left, buttons at
	// the right.
	enabledButton = new QToolButton(headerWidget);
	enabledButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	// Skins may treat "on", every live card's resting state, apart from
	// the engaged look of the other checkable header button.
	enabledButton->setProperty("powerToggle", true);
	enabledButton->setCheckable(true);
	enabledButton->setChecked(descriptor.enabled);
	enabledButton->setIcon(powerToggleIcon(descriptor.enabled));
	connect(enabledButton, &QToolButton::toggled, this, &FilterCardRow::enabledToggled);
	// A line that cannot be switched off (a comment) hides the switch. In a
	// row its place stays empty, so the actions after it keep the x of every
	// other row's actions.
	if (inlineRow)
	{
		QSizePolicy keepPlace = enabledButton->sizePolicy();
		keepPlace.setRetainSizeWhenHidden(true);
		enabledButton->setSizePolicy(keepPlace);
	}
	headerLayout->addWidget(enabledButton);

	// In a one-line row the three bare actions stack in one narrow column,
	// the geometry the legacy rows used: side by side they leave a Filter
	// line too little width for its editor in a 960 px window. The order
	// stays the shared one (docs/skins/README.md).
	QBoxLayout* actionLayout = headerLayout;
	if (inlineRow)
	{
		QVBoxLayout* actionColumn = new QVBoxLayout();
		actionColumn->setContentsMargins(0, 0, 0, 0);
		actionColumn->setSpacing(0);
		headerLayout->addLayout(actionColumn);
		actionLayout = actionColumn;
	}

	addButton = new QToolButton(headerWidget);
	addButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	addButton->setProperty("headerAction", true);
	addButton->setText(QStringLiteral("+"));
	addButton->setToolTip(tr("Add filter above this card"));
	// "+" is a sign, not a name: a screen reader is given the tooltip.
	nameButtonByToolTip(addButton);
	connect(addButton, &QToolButton::clicked, this, &FilterCardRow::addAbove);
	actionLayout->addWidget(addButton);

	removeButton = new QToolButton(headerWidget);
	removeButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	removeButton->setProperty("headerAction", true);
	// A bin, not "-": beside "+" a minus reads as one step of something, and
	// a field report (#456) took it for another control and lost a filter it
	// meant to keep. Same stroke set, size and ink as the code mark below.
	removeButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/trash.svg"),
		QColor(SkinManager::instance()->tokens().text), 14));
	removeButton->setAccessibleName(tr("Remove filter"));
	removeButton->setToolTip(tr("Remove this filter. %1 brings it back.")
		.arg(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText)));
	connect(removeButton, &QToolButton::clicked, this, &FilterCardRow::removeThis);
	actionLayout->addWidget(removeButton);

	editButton = new QToolButton(headerWidget);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setProperty("headerAction", true);
	editButton->setCheckable(true);
	// A code mark ("</>"), not "..." and not a pencil: the ellipsis promised
	// an options menu it never opens, and the pencil collided with the
	// Include card's pencil, which edits the included file's contents. This
	// button edits the config line itself. Rows are rebuilt on every skin
	// switch, so the construction-time ink stays current.
	editButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/code-line.svg"),
		QColor(SkinManager::instance()->tokens().text), 14));
	editButton->setToolTip(tr("Edit raw command"));
	nameButtonByToolTip(editButton);
	connect(editButton, &QToolButton::toggled, this, &FilterCardRow::editTextToggled);
	actionLayout->addWidget(editButton);
	// A clear break between the control column and the identity (badge
	// pictogram, title), so the buttons and the pictogram do not read as one
	// train. 10px was judged barely visible (maintainer, round 2); 30px is
	// the accepted width.
	// A one-line row has no pictogram after the column, and its width is
	// what the editor needs, so the break is left out there.
	if (!inlineRow)
		headerLayout->addSpacing(30);
	else
	{
		// The glyphs differ in width ("+" is text, the others icons); centred,
		// they stand on one vertical axis.
		for (QToolButton* action : { addButton, removeButton, editButton })
			actionLayout->setAlignment(action, Qt::AlignHCenter);
	}
	return headerLayout;
}

void FilterCardRow::buildHeaderIdentity(QHBoxLayout* headerLayout)
{
	typeBadge = new QLabel(headerWidget);
	typeBadge->setObjectName(QStringLiteral("FilterTypeBadge"));
	typeBadge->setAlignment(Qt::AlignCenter);
	typeBadge->setMinimumWidth(46);
	// A one-line row keeps the badge, the summary and the channel strip alive
	// (applyDescriptor feeds them on every edit) but places them differently:
	// no badge and no summary, the title in a fixed cell after the left
	// column, the channel badges at the far end of the line.
	QBoxLayout* rowLayout = inlineRow ? qobject_cast<QBoxLayout*>(cardFrame->layout()) : nullptr;
	if (rowLayout != nullptr)
		typeBadge->setVisible(false);
	else
		headerLayout->addWidget(typeBadge);

	titleLabel = new ElidedLabel(rowLayout != nullptr ? static_cast<QWidget*>(cardFrame) : headerWidget);
	titleLabel->setObjectName(QStringLiteral("FilterCardTitle"));
	titleLabel->setElideMode(Qt::ElideRight);
	titleLabel->setMinimumWidth(92);
	titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	if (rowLayout != nullptr)
	{
		// The cell is fixed so the editors of titled rows start on one axis.
		// Some editors name their unit themselves (rowEditorNamesItself), and
		// then the cell is left out.
		titleLabel->setFixedWidth(InlineTitleWidth);
		titleLabel->setVisible(!rowEditorNamesItself());
		addRowCell(rowLayout, titleLabel, 0);
	}
	else
		headerLayout->addWidget(titleLabel);

	summaryLabel = new ElidedLabel(headerWidget);
	summaryLabel->setObjectName(QStringLiteral("FilterCardSummary"));
	summaryLabel->setElideMode(Qt::ElideRight);
	// No text interaction here: a selectable label consumes the mouse press,
	// which would reduce the row's drag/select surface to the narrow
	// number/title strip. The header is the drag handle (FilterTable
	// hit-tests getHeaderRect); copying the verbatim line is the raw
	// editor's job (the pencil). The label takes its text width and yields
	// (elides) when the header is narrower; the trailing stretch below owns
	// the leftover, so the summary never pushes anything to the far edge.
	summaryLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	if (rowLayout != nullptr)
	{
		// A body-less row's content is its title and this summary, on the
		// one line; a row with an editor has no place for it.
		summaryLabel->setParent(cardFrame);
		summaryLabel->setVisible(!hasEditor);
		addRowCell(rowLayout, summaryLabel, 1);
	}
	else
		headerLayout->addWidget(summaryLabel);

	channelBadgeContainer = new QWidget(rowLayout != nullptr ? static_cast<QWidget*>(cardFrame) : headerWidget);
	// A plain QWidget matches every skin's global "QWidget { background: @BG@ }"
	// rule, and the style then paints that app-background plate over the card
	// surface - a dark rectangle cut around the badges. The widget-level rule
	// outranks any skin sheet, so the strip stays transparent under all skins.
	channelBadgeContainer->setStyleSheet(QStringLiteral("background: transparent;"));
	channelBadgeLayout = new QHBoxLayout(channelBadgeContainer);
	// At the end of a one-line row the strip keeps a gap from the editor.
	channelBadgeLayout->setContentsMargins(rowLayout != nullptr ? 6 : 0, 0, 0, 0);
	channelBadgeLayout->setSpacing(3);
	// In a row the strip can be taller than its badges (the top band), and
	// the badges stand centred in it.
	if (rowLayout != nullptr)
		channelBadgeLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	channelBadgeContainer->setVisible(false);
	// A one-line row adds the strip after the editor (buildBodyStack).
	if (rowLayout != nullptr)
		return;
	headerLayout->addWidget(channelBadgeContainer);
	headerLayout->addStretch(1);
}

void FilterCardRow::buildBodyStack(QVBoxLayout* cardLayout)
{
	bodyStack = new QStackedWidget(cardFrame);
	bodyStack->setObjectName(QStringLiteral("FilterCardBody"));
	bodyStack->setAttribute(Qt::WA_StyledBackground, true);
	// Stop an overgrown body editor (e.g. the legacy DeviceFilterGUI's
	// QTreeWidget with sizeAdjustPolicy=AdjustToContents, or any other GUI that
	// reports a content-driven sizeHint) from propagating its preferred width
	// up through the card. Otherwise the card grows past the visible viewport
	// and everything past the header's left column (badges, body editors)
	// renders thousands of pixels off screen and becomes invisible.
	// Ignored sizePolicy alone is not enough: it stops sizeHint propagation
	// but the layout system still inherits the inner widgets' minimumSize.
	// Pin the bodyStack's own minimumSize to 0 so the card frame's minimum
	// width is driven by the header only.
	bodyStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	bodyStack->setMinimumSize(0, 0);
	if (inlineRow)
	{
		// Across the line the editor takes what the left column and the
		// title cell leave. The raw line editor stands beside the stack, not
		// in it, so engaging it shows one line instead of a box as tall as the
		// editor it replaces (a stack is as tall as its tallest page). The
		// channel badges close the line.
		cardLayout->addWidget(bodyStack, 1);
		lineEdit = new QLineEdit(cardFrame);
		lineEdit->setObjectName(QStringLiteral("FilterCardRawEditor"));
		connect(lineEdit, &QLineEdit::editingFinished, this, &FilterCardRow::lineEditingFinished);
		lineEdit->setVisible(false);
		addRowCell(cardLayout, lineEdit, 1);
		addRowCell(cardLayout, channelBadgeContainer, 0);
		return;
	}
	cardLayout->addWidget(bodyStack);

	lineEdit = new QLineEdit(bodyStack);
	lineEdit->setObjectName(QStringLiteral("FilterCardRawEditor"));
	connect(lineEdit, &QLineEdit::editingFinished, this, &FilterCardRow::lineEditingFinished);
	bodyStack->addWidget(lineEdit);
}

void FilterCardRow::mountBody()
{
	// A Copy line with inline `expression` factors must not open the routing
	// view (FilterCardModel::opensRoutingView): such lines keep the raw body
	// (dynamic-value contract), like every other dynamic line without a
	// dynamic-capable editor.
	IRoutingRenderer* routingRenderer = FilterCardModel::opensRoutingView(descriptor)
		? SkinManager::instance()->routingRenderer() : nullptr;

	if (routingRenderer != nullptr)
	{
		// Skin-specific Copy routing view (crosspoint matrix, step list, ...).
		// The view owns its working routing state; on edit we serialise it
		// back into item->text.
		mountEditorBody([this, routingRenderer](QWidget* editorContainer) -> QWidget* {
			std::vector<Assignment> routingAssignments = CopyRoutingAdapter::parse(descriptor.parameters);
			// Seed the routing editor with the real device channel set (L, R, C, ...),
			// the same list the legacy CopyFilterGUI starts from in setChannelFlow.
			// Without it the graph only shows channels already named in the line, so
			// the user cannot route to/from a channel that has no assignment yet
			// (e.g. copying L onto R when R is not referenced).
			std::vector<std::wstring> channelNames = this->table->getChannelNames();
			// Copy uses the default port model: symmetric sources/targets seeded
			// from the device channels, with editable factors. The same device
			// channels decide which channels the view draws as virtual.
			RoutingPortModel portModel;
			portModel.deviceChannels = channelNames;
			routingView = routingRenderer->create(routingAssignments, channelNames, portModel, editorContainer,
				SkinManager::instance()->tokens());
			// A board wider than the card scrolls sideways (RoutingViewScroll.h).
			return new RoutingViewScroll(routingView, editorContainer);
		});
		connect(routingView, &RoutingView::routingChanged, this, &FilterCardRow::routingEdited);
		connect(routingView, &RoutingView::routingChanged, table, &FilterTable::updateChannels);

		// The expand default (buildHeaderControls) keys off the gui the body shows; routing
		// rows are their own editor and start open.
		setExpanded(true);
	}
	else if (gui != nullptr)
	{
		mountEditorBody([this](QWidget*) -> QWidget* {
			return this->gui;
		});
		connect(gui, &IFilterGUI::updateModel, this, &FilterCardRow::cardEdited);
	}
	else
	{
		// Unrecognized command line: no dedicated editor. Present a styled
		// monospace raw card with the command token emphasized, so it reads
		// as a deliberate raw row instead of bare plain text.
		mountRawBody();
	}
}

void FilterCardRow::mountRawBody()
{
	const SkinTokens& tk = SkinManager::instance()->tokens();
	QWidget* rawContainer = new QWidget(bodyStack);
	rawContainer->setObjectName(QStringLiteral("FilterCardEditor"));
	rawContainer->setAttribute(Qt::WA_StyledBackground, true);
	rawContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	rawContainer->setMinimumSize(0, 0);
	QHBoxLayout* rawLayout = new QHBoxLayout(rawContainer);
	rawLayout->setContentsMargins(12, 10, 12, 12);
	rawLayout->setSpacing(10);

	QLabel* glyph = new QLabel(QStringLiteral(">_"), rawContainer);
	glyph->setObjectName(QStringLiteral("FilterCardRawGlyph"));
	glyph->setStyleSheet(QStringLiteral("color:%1; font-family:\"%2\"; font-weight:700;")
		.arg(tk.mutedText, tk.monoFontFamily));
	rawLayout->addWidget(glyph, 0, Qt::AlignTop);

	QLabel* rawLabel = new QLabel(rawContainer);
	rawLabel->setObjectName(QStringLiteral("FilterCardRawText"));
	rawLabel->setText(item->text);
	rawLabel->setWordWrap(true);
	rawLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	rawLabel->setStyleSheet(QStringLiteral("QLabel#FilterCardRawText { background:%1; color:%2; border:1px solid %3; border-radius:%4px; padding:6px 10px; font-family:\"%5\"; }")
		.arg(tk.surfaceSunken, tk.text, tk.border)
		.arg(tk.borderRadius / 2)
		.arg(tk.monoFontFamily));
	rawLayout->addWidget(rawLabel, 1);

	bodyStack->addWidget(rawContainer);
	bodyStack->setCurrentWidget(rawContainer);
}

void FilterCardRow::setChannelFlow(const ChannelFlowAtLine& flow)
{
	// The Copy routing rows have no gui; what their line adds to the names
	// in scope below is computed from the line itself (computeChannelFlow).
	if (gui != nullptr)
		gui->setChannelFlow(flow);
}

CommandRowInfo FilterCardRow::currentRowInfo() const
{
	// The derivation is CommandRowFacts' (EditorLogicTests pins it); this
	// widget only gathers the facts it holds: selection, the active skin's
	// lane metrics and the analysis engine's load facts for its line.
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	CommandRowFacts::RowContext context;
	context.selected = table != nullptr && table->getSelectedItems().contains(item);
	context.focused = table != nullptr && table->getFocusedItem() == item;
	context.laneUnit = tokens.channelGroupIndent;
	context.rowHeight = tokens.rowHeight;
	context.logicSiblingsIndentAsMembers = SkinManager::instance()->logicSiblingsIndentAsMembers();
	CommandRowInfo info = CommandRowFacts::describe(descriptor, context,
		table != nullptr ? table->loadTraceFactsForRow(rowNumber - 1) : QList<ConfigLoadTraceEntry>());
	info.inlineRow = inlineRow;
	// A row centres its controls on its top band, or on the whole row when
	// it centres its markers (Copy), not on a strip of rowHeight at its top,
	// so a scope lane has to meet it there. Before the first layout a
	// centred row's height is unknown and the band value stands in.
	if (inlineRow && centresRowMarkers() && height() > 0)
		info.headerCenterY = height() / 2.0;
	else if (inlineRow)
		info.headerCenterY = CommandRowInfo::cardMarginTop + 1 + InlineBandHeight / 2.0;
	return info;
}

QRect FilterCardRow::getHeaderRect() const
{
	// In this row's coordinates: headerWidget->pos() alone is relative to
	// cardFrame and would miss the outer card margins, shifting the drag
	// hit-test area away from the header the user actually sees.
	return QRect(headerWidget->mapTo(this, QPoint(0, 0)), headerWidget->size());
}

void FilterCardRow::editText()
{
	if (!editButton->isChecked())
		editButton->setChecked(true);
}

void FilterCardRow::updateRowPosition(int rowNumber, FilterCardRowScope scope)
{
	// The constructor puts the number into numberLabel and the scope into
	// the outer layout's left margin, the descriptor (scope rail painting)
	// and the skin styling hooks. Refresh those in place.
	this->rowNumber = rowNumber;
	if (numberLabel != nullptr)
	{
		// A skin's prepareCommandRow may have rewritten the plain number into
		// its own coordinate grammar at construction (MatrixSkin: "3" -> "B3",
		// bus letter + line). The prefix only depends on the command type,
		// which does not change for a shifted row, so keep any non-digit
		// prefix and replace just the trailing line number - the same text a
		// full rebuild would produce.
		const QString text = numberLabel->text();
		int digitStart = int(text.size());
		while (digitStart > 0 && text.at(digitStart - 1).isDigit())
			digitStart--;
		numberLabel->setText(text.left(digitStart) + QString::number(rowNumber));
	}

	if (descriptor.depth != scope.indent || descriptor.logicDepth != scope.logic
		|| descriptor.scopeChannels != scope.channels)
	{
		descriptor.depth = scope.indent;
		descriptor.logicDepth = scope.logic;
		descriptor.scopeChannels = scope.channels;
		if (layout() != nullptr)
			layout()->setContentsMargins(CommandRowInfo::cardMarginLeft + rowIndentUnits() * SkinManager::instance()->tokens().channelGroupIndent,
				CommandRowInfo::cardMarginTop, 8, 4);
		applyDescriptor();
	}
	else
	{
		update();
	}
}

QSize FilterCardRow::sizeHint() const
{
	// Blank lines collapse to a thin spacer; no header, no body, just a few
	// pixels of breathing room so visual grouping in the config survives.
	if (descriptor.kind == FilterCardKind::Spacer)
	{
		int preferredWidth = table != nullptr ? table->getPreferredWidth() : 0;
		return QSize(preferredWidth, 10);
	}

	// Use only the header's preferred width and the viewport - never the body's
	// content-driven sizeHint. Some legacy filter GUIs (DeviceFilterGUI with
	// QTreeWidget AdjustToContents, VST / Convolution / Stage editors) report
	// a sizeHint matching their full content (thousands of pixels), which
	// would otherwise inflate the entire QGridLayout column in FilterTable and
	// push the cards' content far off screen.
	int height = QWidget::sizeHint().height();
	int width = headerWidget ? headerWidget->sizeHint().width() : QWidget::sizeHint().width();
	if (layout() != nullptr)
		width += layout()->contentsMargins().left() + layout()->contentsMargins().right();
	int preferredWidth = table != nullptr ? table->getPreferredWidth() : 0;
	if (width < preferredWidth)
		width = preferredWidth;
	return QSize(width, height);
}

QSize FilterCardRow::minimumSizeHint() const
{
	// Spacer rows force a small fixed height so QGridLayout does not promote
	// them up to the body-driven minimum of neighbouring cards.
	if (descriptor.kind == FilterCardKind::Spacer)
		return QSize(0, 10);

	// Cap the cell's minimum width at a small constant so the QGridLayout in
	// FilterTable does not inherit any content-driven minimum from a card's
	// body editor. Without this, ONE row with a wide legacy filter GUI forces
	// EVERY card column wide enough to push the cards' content far off
	// screen. Height stays layout-driven so cards still vertically size to
	// fit their bodies.
	int height = QWidget::minimumSizeHint().height();
	return QSize(0, height);
}

// The editor scroll wrapper pins the body's WIDTH (Ignored policy), but its
// height cannot come from Qt's own hint plumbing: QScrollArea samples the
// editor's sizeHint before the editor ever has a real width, and for a
// wrapping layout (the Device card's FlowLayout) that early hint is one item
// wide - its height-for-width answer is a stacked column, and the stale
// answer never gets re-queried once the real width arrives. So the row
// follows the content explicitly: whenever the scroll or its content resizes
// or relayouts, the scroll's height is fixed to what the content needs at
// the width it actually has.
void FilterCardRow::watchPointerSelection(QWidget* root)
{
	if (root == nullptr)
		return;

	root->installEventFilter(this);
	for (QWidget* child : root->findChildren<QWidget*>(
		QString(), Qt::FindDirectChildrenOnly))
	{
		watchPointerSelection(child);
	}
}

void FilterCardRow::mountEditorBody(const std::function<QWidget*(QWidget* editorContainer)>& createContent)
{
	QWidget* editorContainer = new QWidget(bodyStack);
	editorContainer->setObjectName(QStringLiteral("FilterCardEditor"));
	editorContainer->setAttribute(Qt::WA_StyledBackground, true);
	// Match bodyStack: stop overgrown editors from inflating the card width.
	editorContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	editorContainer->setMinimumSize(0, 0);
	QVBoxLayout* editorLayout = new QVBoxLayout(editorContainer);
	// A one-line row has no strip above the editor to clear, and its height
	// is the editor's plus this margin, so only a thin border stays.
	if (inlineRow)
		editorLayout->setContentsMargins(4, 4, 0, 4);
	else
		editorLayout->setContentsMargins(12, 10, 12, 12);

	QWidget* content = createContent(editorContainer);

	// Wrap the editor in a borderless scroll area. Without this, any editor
	// that has a content-driven sizeHint (DeviceFilterGUI's QTreeWidget
	// AdjustToContents, GraphicEQ / Convolution / VSTPlugin's internal
	// AdjustToContents widgets) propagates a huge minimumSize up through
	// editorContainer/bodyStack/cardFrame/FilterCardRow and the QGridLayout
	// in FilterTable then forces every card column to that width, pushing
	// the cards' content thousands of pixels off screen.
	QScrollArea* scroll = new QScrollArea(editorContainer);
	scroll->setObjectName(QStringLiteral("FilterCardEditorScroll"));
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidgetResizable(true);
	scroll->setMinimumSize(0, 0);
	scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	scroll->setWidget(content);
	watchEditorScroll(scroll);
	editorLayout->addWidget(scroll);

	bodyStack->addWidget(editorContainer);
	bodyStack->setCurrentWidget(editorContainer);
}

bool FilterCardRow::fitEditorScrollHeight(QScrollArea* scroll, int contentHeight)
{
	// The body is as tall as its content, so it never scrolls on its own:
	// the list is the one thing that scrolls vertically. This used to stop at
	// 600 px with the vertical bar off, and a body taller than that (a
	// MultiConvolution mapping opened over a 7.1.4 endpoint is 794 px in one
	// skin) moved under the wheel inside its card with nothing on screen to
	// say so.
	//
	// The guard that remains is for a hint that is not a real height: legacy
	// filter GUIs can report tens of thousands of pixels (the same hints the
	// width clamp exists for), and such a body must not swallow the table.
	// Past the guard the body does scroll, and then it shows the bar.
	const int runaway = 4000;
	const bool capped = contentHeight > runaway;
	const Qt::ScrollBarPolicy policy = capped ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
	if (scroll->verticalScrollBarPolicy() != policy)
		scroll->setVerticalScrollBarPolicy(policy);

	// A body wider than the card (a grid routing view over a file with many
	// channels) scrolls sideways, and its bar takes room below the content.
	// Without that room the bar lies over the last row.
	const QScrollBar* across = scroll->horizontalScrollBar();
	const int barRoom = across != nullptr && across->maximum() > across->minimum()
		? across->sizeHint().height() : 0;

	const int desired = qBound(24, contentHeight, runaway) + barRoom;
	if (scroll->minimumHeight() == desired && scroll->maximumHeight() == desired)
		return false;
	scroll->setFixedHeight(desired);
	return true;
}

void FilterCardRow::watchEditorScroll(QScrollArea* scroll)
{
	scroll->installEventFilter(this);
	if (scroll->widget() != nullptr)
		scroll->widget()->installEventFilter(this);
	// The sideways bar comes and goes with the card's width, and neither a
	// Resize nor a LayoutRequest of the body says so.
	connect(scroll->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this, scroll]() {
		syncEditorScrollHeight(scroll);
	});
	syncEditorScrollHeight(scroll);
}

void FilterCardRow::syncEditorScrollHeight(QScrollArea* scroll)
{
	QWidget* content = scroll->widget();
	if (content == nullptr)
		return;

	// The content is never narrower than its own minimum; past that it
	// scrolls sideways instead of wrapping.
	const int width = qMax(scroll->viewport()->width(), content->minimumSizeHint().width());
	const int desired = content->hasHeightForWidth() && width > 0
		? content->heightForWidth(width)
		: content->sizeHint().height();
	if (fitEditorScrollHeight(scroll, desired))
	{
		// The new height must reach the FilterTable grid, but the layout
		// chain above the scroll (editor container -> body stack -> card
		// frame -> row) re-validates lazily hop by hop and the cascade can
		// stall with a stale cached minimum mid-chain - observed as "the
		// card never grows" when the routing views' channel fold expands.
		// Invalidate the whole chain explicitly so the next grid pass
		// re-queries a fresh row minimum.
		//
		// Each hop also announces its own new geometry. A layout keeps the
		// size hint of every widget it holds until that widget says its
		// geometry changed, so invalidating the layouts alone left the body
		// stack's old hint in the card frame's layout: a card whose body
		// lost its sideways scroll bar during the first layout passes stayed
		// that bar's height too tall until something else rebuilt the chain.
		for (QWidget* w = scroll->parentWidget(); w != nullptr && w != this; w = w->parentWidget())
		{
			if (w->layout() != nullptr)
				w->layout()->invalidate();
			w->updateGeometry();
		}
		if (layout() != nullptr)
			layout()->invalidate();
		updateGeometry();
	}
}

bool FilterCardRow::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
	case QEvent::ChildPolished:
	{
		QChildEvent* childEvent = static_cast<QChildEvent*>(event);
		watchPointerSelection(qobject_cast<QWidget*>(childEvent->child()));
		break;
	}
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	{
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (event->type() == QEvent::MouseButtonPress
			&& mouseEvent->button() == Qt::LeftButton
			&& mouseEvent->modifiers() == Qt::NoModifier
			&& table != nullptr && item != nullptr)
		{
			table->selectOnlyFromCard(item);
		}
		// This press already closed the raw editor by taking its focus
		// (lineEditingFinished). The toggle must not see it, or its release
		// switches editing straight back on.
		if (watched == editButton && editorClosedByFocusLoss && mouseEvent->button() == Qt::LeftButton)
			return true;
		break;
	}
	case QEvent::FocusIn:
	{
		// A press already moved the list's focus marker (above). Keyboard focus
		// that arrives by Tab has to move it too: controls inside a card show
		// focus only through the card's own frame, so without this a tabbed-in
		// control showed nothing and another card could stay lit.
		const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
		if ((reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason)
			&& table != nullptr && item != nullptr)
			table->focusFromCard(item);
		break;
	}
	case QEvent::Resize:
	case QEvent::Show:
	case QEvent::LayoutRequest:
	{
		// Watched objects are the editor scroll itself and its content widget
		// (whose parent chain is content -> viewport -> scroll).
		QScrollArea* scroll = qobject_cast<QScrollArea*>(watched);
		if (scroll == nullptr && watched->parent() != nullptr)
			scroll = qobject_cast<QScrollArea*>(watched->parent()->parent());
		if (scroll != nullptr)
			syncEditorScrollHeight(scroll);
		break;
	}
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}

void FilterCardRow::addRowCell(QBoxLayout* layout, QWidget* cell, int stretch)
{
	// The raw line editor is shown only while the editor beside it is
	// hidden, and the row is then one band tall, so centring it keeps it in
	// the band too.
	if (hasEditor && cell != lineEdit)
	{
		cell->setFixedHeight(InlineBandHeight);
		layout->addWidget(cell, stretch, centresRowMarkers() ? Qt::AlignVCenter : Qt::AlignTop);
	}
	else
		layout->addWidget(cell, stretch, Qt::AlignVCenter);
}

bool FilterCardRow::centresRowMarkers() const
{
	// In a Send or MultiConvolution row the routing board is a section under
	// the row's main line, so the row's markers stand beside that line at the
	// top. A Copy row has no main line: the board is the row, so its markers
	// (left column, title, channel badges) are centred on the whole row. The
	// GraphicEQ row (its plot) and the IIR coefficient row (its table) are
	// one body for the same reason. The IIR row shares the biquad kind with
	// the one-line Filter and the all-pass rows, so its editor tells it.
	if (!inlineRow || !hasEditor)
		return false;
	return descriptor.kind == FilterCardKind::Copy || descriptor.kind == FilterCardKind::GraphicEq
		|| (gui != nullptr && gui->inherits("IIRCardEditor"));
}

bool FilterCardRow::rowEditorNamesItself() const
{
	// The one-line biquad editor's type selector names the unit (Peaking,
	// Low-shelf, ...). The VST unit's module readout says VST, and the
	// subwoofer routing view prints its own vertical name.
	if (gui != nullptr && gui->inherits("BiQuadFilterGUI"))
		return true;
	return descriptor.kind == FilterCardKind::Vst || descriptor.kind == FilterCardKind::SubwooferRouting;
}

void FilterCardRow::setRowTitle(const QString& title)
{
	// The cell is narrow, so a two-word title wraps onto a second line
	// instead of being cut ("Hilbert transform"). A single word wider than
	// the cell is split before its inner capitals ("MultiConvolution" into
	// MULTI over CONVOLUTION), without a hyphen. Only a word that still does
	// not fit after that is elided as in the card header.
	titleLabel->ensurePolished();
	const QFontMetrics metrics = titleLabel->fontMetrics();
	QStringList words;
	bool wordsFit = true;
	for (const QString& word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts))
	{
		if (metrics.horizontalAdvance(word) <= InlineTitleWidth)
		{
			words.append(word);
			continue;
		}
		int start = 0;
		for (int i = 1; i <= word.size(); i++)
		{
			if (i < word.size() && !(word.at(i).isUpper() && word.at(i - 1).isLower()))
				continue;
			const QString part = word.mid(start, i - start);
			wordsFit = wordsFit && metrics.horizontalAdvance(part) <= InlineTitleWidth;
			words.append(part);
			start = i;
		}
	}
	titleLabel->setWordWrap(wordsFit);
	if (wordsFit)
	{
		// ElidedLabel elides whatever it holds as full text; plain QLabel text
		// lets the wrap through.
		titleLabel->setFullText(QString());
		titleLabel->setText(words.join(QLatin1Char(' ')));
		titleLabel->setToolTip(title);
	}
	else
		titleLabel->setFullText(title);
}

bool FilterCardRow::rawEditing() const
{
	if (lineEdit == nullptr)
		return false;
	return inlineRow ? !lineEdit->isHidden() : bodyStack->currentWidget() == lineEdit;
}

void FilterCardRow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	// The junction a scope lane meets depends on a one-line row's height
	// (currentRowInfo), which only a resize changes.
	if (inlineRow)
		syncVisualState();
}

int FilterCardRow::rowIndentUnits() const
{
	return CommandRowFacts::indentUnits(descriptor, SkinManager::instance()->logicSiblingsIndentAsMembers());
}

void FilterCardRow::paintEvent(QPaintEvent*)
{
	// The active skin may own the whole scope gutter (the If-block lanes);
	// the shared channel rail below stays the default for skins that do not
	// answer.
	{
		QPainter gutterPainter(this);
		if (SkinManager::instance()->paintScopeGutter(gutterPainter, size(), visualInfo))
			return;
	}

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	if (descriptor.depth <= 0)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QColor color(descriptor.color);
	int indent = 8 + (descriptor.depth - 1) * tokens.channelGroupIndent;
	QRect lineRect(indent, 0, tokens.channelGroupIndent, height());

	switch (tokens.channelGroupStyle)
	{
	case SkinTokens::TreeLines:
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(lineRect.left() + 7, 0, lineRect.left() + 7, height());
		painter.drawText(QRect(lineRect.left() + 1, 0, 16, tokens.rowHeight), Qt::AlignCenter, QStringLiteral("|"));
		break;
	case SkinTokens::DottedLine:
		painter.setPen(QPen(color, 1, Qt::DotLine));
		painter.drawLine(lineRect.left() + 5, 0, lineRect.left() + 5, height());
		break;
	case SkinTokens::GradientBar:
	default:
	{
		QLinearGradient gradient(lineRect.left(), 0, lineRect.left(), height());
		QColor start = color;
		start.setAlpha(55);
		QColor end = color;
		end.setAlpha(12);
		gradient.setColorAt(0, start);
		gradient.setColorAt(1, end);
		painter.fillRect(QRect(lineRect.left() + 7, 0, 3, height()), gradient);
		break;
	}
	}
}

// Renders the badge pictogram at the label's device pixel ratio in the
// skin-resolved ink. The toolbar's GUIHelper::tintedIcon
// bakes a DPR-1 pixmap for QIcon consumers; a QLabel needs an explicit
// DPR-aware pixmap or the glyph blurs on scaled displays. Overshooting the
// fill past the device-independent size is harmless - untouched pixels have
// no coverage for CompositionMode_SourceIn to paint on.
static QPixmap badgePictogram(const QString& resource, const QColor& ink, int size, qreal devicePixelRatio)
{
	const QString cacheKey = QStringLiteral("FilterCardBadge:%1:%2:%3:%4")
		.arg(resource, ink.name(QColor::HexArgb))
		.arg(size)
		.arg(devicePixelRatio, 0, 'f', 3);
	QPixmap cached;
	if (QPixmapCache::find(cacheKey, &cached))
		return cached;

	QPixmap pixmap = QIcon(resource).pixmap(QSize(size, size), devicePixelRatio);
	if (pixmap.isNull())
		return pixmap;
	QPainter painter(&pixmap);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(pixmap.rect(), ink);
	painter.end();
	QPixmapCache::insert(cacheKey, pixmap);
	return pixmap;
}

void FilterCardRow::rebuildSummary()
{
	// The line alone cannot tell its scope, so the one the row was built
	// under (constructor / updateRowPosition, from the document walk) carries
	// over.
	descriptor = FilterCardModel::describeRow(item->text, FilterCardModel::scopeOf(descriptor));
	applyDescriptor();
}

void FilterCardRow::applyDescriptor()
{

	// Blank lines render as a thin spacer: no header, no body.
	// The card frame itself stays visible (so its background fills the gap and
	// scope-rail painting still works for indented blocks) but is collapsed
	// to a small fixed height by sizeHint() / minimumSizeHint() below.
	const bool isSpacer = descriptor.kind == FilterCardKind::Spacer;
	// Hiding the focused subtree would tab to another card and scroll it into
	// view. The table keeps keyboard navigation at the current selection.
	QWidget* focused = QApplication::focusWidget();
	if (table != nullptr && focused != nullptr
		&& ((isSpacer && isAncestorOf(focused))
			|| (!expanded && bodyStack != nullptr && bodyStack->isAncestorOf(focused))))
		table->setFocus(Qt::OtherFocusReason);
	if (headerWidget != nullptr)
		headerWidget->setVisible(!isSpacer);
	if (bodyStack != nullptr)
		bodyStack->setVisible(!isSpacer && expanded && !(inlineRow && rawEditing()));
	if (isSpacer)
	{
		syncVisualState();
		updateGeometry();
		update();
		return;
	}

	const BadgeTreatment badgeTreatment = SkinManager::instance()->badgeTreatment(
		currentRowInfo(), descriptor.color, descriptor.badge);
	// The badge chrome and pictogram ink are one skin-owned decision.
	// Only touch the widget when the style actually changed: setStyleSheet
	// unconditionally rebuilds the widget's style, and applyDescriptor runs
	// again on every summary rebuild.
	if (typeBadge->styleSheet() != badgeTreatment.qss)
		typeBadge->setStyleSheet(badgeTreatment.qss);
	// The monogram survives only for lines the icon
	// catalog does not map (raw text), so unknown commands keep reading
	// instead of going blank.
	const QString badgeIcon = FilterCardModel::badgeIconResource(descriptor.type, descriptor.badge);
	if (badgeIcon.isEmpty())
	{
		typeBadge->setPixmap(QPixmap());
		typeBadge->setText(descriptor.badge);
	}
	else
	{
		typeBadge->setText(QString());
		typeBadge->setPixmap(badgePictogram(badgeIcon, badgeTreatment.ink, 16, devicePixelRatioF()));
	}
	if (inlineRow)
		setRowTitle(descriptor.title);
	else
		titleLabel->setFullText(descriptor.title);
	summaryLabel->setFullText(descriptor.summary);
	syncSummaryVisibility();
	// A line the engine could not use says why on hover. The analysis run is what
	// produces the reason, so this is empty until one has happened and goes stale
	// on edit, like every other load fact.
	// A setup failure wins over a parse error: it is the one that kept the
	// whole configuration from playing.
	const CommandRowInfo rowInfo = currentRowInfo();
	if (rowInfo.setupFailed)
		summaryLabel->setToolTip(tr("Equalizer APO could not prepare this filter, so the whole configuration was not applied and the previous settings keep playing. Check the file or plug-in this line uses, or switch the line off."));
	else if (!rowInfo.parseError.isEmpty())
		summaryLabel->setToolTip(tr("This line was not applied: %1").arg(rowInfo.parseError));
	else
		summaryLabel->setToolTip(descriptor.summary);
	// A row with an editor hides the summary, so the reason the engine gave
	// would be lost; the line number at the left carries it instead. Only
	// the reason: the plain summary would repeat what the editor shows.
	if (inlineRow)
		numberLabel->setToolTip(rowInfo.setupFailed || !rowInfo.parseError.isEmpty()
			? summaryLabel->toolTip() : QString());
	enabledButton->blockSignals(true);
	enabledButton->setChecked(descriptor.enabled);
	enabledButton->setIcon(powerToggleIcon(descriptor.enabled));
	enabledButton->blockSignals(false);
	enabledButton->setVisible(descriptor.canToggleEnabled);
	syncHeaderToolTips();
	// Disable only the body editor when the line is commented out. This keeps
	// the card frame (and its header buttons - including the enable toggle and
	// the raw-edit affordance) interactive so the user can flip the line back on.
	// A pure comment row is "disabled" by definition (the line starts with '#'),
	// but its body IS the note editor - keep it editable.
	if (gui != nullptr)
		gui->setEnabled(descriptor.enabled || descriptor.kind == FilterCardKind::Comment);
	// The Copy routing view is the body of a Copy row (gui is null there), so
	// it follows the same rule; before this it stayed live on a switched-off
	// line (audit #348 TD-04).
	if (routingView != nullptr)
		routingView->setEnabled(descriptor.enabled);
	// A row's own channel list (the Channel card's selection, Copy's device
	// destinations) wins. Other rows inside a Channel: selection inherit the
	// selection's badges, so the group's reach is readable on every member
	// row instead of only on its head - but only for row types the engine
	// actually narrows to the selection; control rows, notes and raw text
	// would claim an influence they do not have. The device list is the one
	// the Copy routing view judges virtual channels by, so a target the body
	// draws dashed never shows in the header.
	const std::vector<std::wstring> deviceChannels = table != nullptr
		? table->getChannelNames() : std::vector<std::wstring>();
	buildChannelBadges(FilterCardModel::headerChannels(descriptor, deviceChannels), deviceChannels);
	syncVisualState();
	update();
}

void FilterCardRow::buildChannelBadges(const QStringList& channels,
	const std::vector<std::wstring>& deviceChannels)
{
	if (channels == renderedChannelBadges && deviceChannels == renderedBadgeDeviceChannels)
		return;
	renderedChannelBadges = channels;
	renderedBadgeDeviceChannels = deviceChannels;

	while (QLayoutItem* child = channelBadgeLayout->takeAt(0))
	{
		// A one-line row's strip holds a column layout per pair of badges.
		if (QLayout* column = child->layout())
		{
			while (QLayoutItem* inner = column->takeAt(0))
			{
				delete inner->widget();
				delete inner;
			}
		}
		delete child->widget();
		delete child;
	}

	const QStringList shown = channels.mid(0, 8);
	if (inlineRow)
	{
		// At the end of a one-line row the badges stand two to a column, as
		// the actions do at its start: in a single file the strip took the
		// width the Filter editor needs in a 960 px window.
		for (int i = 0; i < shown.size(); i += 2)
		{
			QVBoxLayout* column = new QVBoxLayout();
			column->setContentsMargins(0, 0, 0, 0);
			column->setSpacing(2);
			for (int j = i; j < qMin(i + 2, int(shown.size())); j++)
				column->addWidget(new ChBadge(shown.at(j),
					ChannelIdentity::isVirtual(shown.at(j), deviceChannels), channelBadgeContainer));
			channelBadgeLayout->addLayout(column);
		}
	}
	else
	{
		for (const QString& channel : shown)
			channelBadgeLayout->addWidget(new ChBadge(channel,
				ChannelIdentity::isVirtual(channel, deviceChannels), channelBadgeContainer));
	}
	channelBadgeContainer->setVisible(!channels.isEmpty());
}

void FilterCardRow::cardEdited()
{
	IFilterGUI* senderGui = qobject_cast<IFilterGUI*>(QObject::sender());
	if (senderGui == nullptr)
		return;

	QString command;
	QString parameters;
	senderGui->store(command, parameters);
	table->replaceLineCommand(item, command, parameters);
	rebuildSummary();
}

void FilterCardRow::routingEdited()
{
	if (routingView == nullptr)
		return;

	const QString parameters = CopyRoutingAdapter::serialize(routingView->assignments());
	table->replaceLineCommand(item, QStringLiteral("Copy"), parameters);
	rebuildSummary();
}

void FilterCardRow::addAbove()
{
	FilterTemplate filterTemplate;
	// A raw edit can replace this row while the picker runs its event loop.
	// Keep the document anchor, not members of the possibly deleted widget.
	FilterTable* targetTable = table;
	FilterTable::Item* targetItem = item;
	if (targetTable->chooseFilterTemplate(&filterTemplate,
		addButton->mapToGlobal(QPoint(0, addButton->height())), this)
		&& targetTable->documentItems().contains(targetItem))
	{
		// A card header's + belongs to that card's leading edge: insertLine
		// takes an insert-before anchor, so this row itself is the desired
		// anchor. Only the new card is built; this row and every other one
		// stay put, and with them the scroll position.
		targetTable->insertLine(filterTemplate.getLine(), targetItem);
		targetTable->setFocus(Qt::OtherFocusReason);
	}
}

void FilterCardRow::removeThis()
{
	// Deferred because removeLine deletes this very widget, which is still
	// on the dispatch path of its own header button.
	FilterTable* targetTable = table;
	FilterTable::Item* targetItem = item;
	QTimer::singleShot(0, targetTable, [targetTable, targetItem]() {
		targetTable->removeLine(targetItem);
	});
}

void FilterCardRow::editTextToggled(bool checked)
{
	setEditing(checked);
}

void FilterCardRow::setEditing(bool editing)
{
	if (inlineRow)
	{
		// A row swaps its editor or content for the one-line raw editor
		// beside it, and back; it never folds, so expanded is left alone.
		if (editing)
			lineEdit->setText(item->text);
		bodyStack->setVisible(!editing && expanded);
		lineEdit->setVisible(editing);
		syncSummaryVisibility();
		if (editing)
		{
			lineEdit->setFocus();
			lineEdit->selectAll();
		}
		return;
	}
	if (editing)
	{
		lineEdit->setText(item->text);
		bodyStack->setCurrentWidget(lineEdit);
		setExpanded(true);
		lineEdit->setFocus();
		lineEdit->selectAll();
	}
	else if (gui != nullptr || bodyStack->count() > 1)
	{
		bodyStack->setCurrentIndex(1);
	}
}

void FilterCardRow::lineEditingFinished()
{
	if (rawEditing() && !editingDone)
	{
		// Qt hands the focus to the nearest click-focus ancestor (the list)
		// before it delivers a press, so a press on the raw-line toggle closes
		// the editor here first. The flag lives until the event loop runs
		// again, long enough for the toggle to see that same press and swallow
		// it; letting it through reopened the editor on release (#456).
		editorClosedByFocusLoss = true;
		QTimer::singleShot(0, this, [this]() { editorClosedByFocusLoss = false; });
		editingDone = true;
		if (lineEdit->text() != item->text)
		{
			table->setLineText(item, lineEdit->text());
			editingDone = false;
			// In-place like the enabled toggle: only this row's card changes
			// (the rows below re-scope in place), instead of rebuilding every
			// card and dropping the scroll position.
			FilterTable* targetTable = table;
			FilterTable::Item* targetItem = item;
			QTimer::singleShot(0, targetTable, [targetTable, targetItem]() {
				targetTable->updateSingleRowGui(targetItem);
			});
			return;
		}
		editButton->setChecked(false);
		editingDone = false;
	}
}

void FilterCardRow::enabledToggled(bool checked)
{
	if (!descriptor.canToggleEnabled)
		return;

	if (enabledButton != nullptr)
		enabledButton->setIcon(powerToggleIcon(checked));
	syncHeaderToolTips();

	table->setLineEnabled(item, checked);
	FilterTable* targetTable = table;
	FilterTable::Item* targetItem = item;
	// In-place refresh: only the toggled row's GUI needs to change. A full
	// updateGuis() on a 500+ row config is the dominant source of toggle
	// latency.
	QTimer::singleShot(0, targetTable, [targetTable, targetItem]() {
		targetTable->updateSingleRowGui(targetItem);
	});
}

void FilterCardRow::setExpanded(bool open)
{
	expanded = open;
	expandButton->setText(open ? QStringLiteral("v") : QStringLiteral(">"));
	// A row's raw line editor stands beside the stack, so it counts as body
	// here: a row folded from code while it is being typed in must not keep
	// the focus on it either.
	QWidget* focused = QApplication::focusWidget();
	if (!open && table != nullptr
		&& (bodyStack->isAncestorOf(focused) || (inlineRow && focused == lineEdit)))
		table->setFocus(Qt::OtherFocusReason);
	bodyStack->setVisible(open && !(inlineRow && rawEditing()));
	syncSummaryVisibility();
	syncHeaderToolTips();
}

void FilterCardRow::syncHeaderToolTips()
{
	// Each control says what a click does from here, so the power switch
	// and the fold arrow follow their state. Both show a sign or an icon
	// alone, so the same sentence is their name for a screen reader.
	expandButton->setToolTip(expanded ? tr("Collapse filter card") : tr("Expand filter card"));
	nameButtonByToolTip(expandButton);
	enabledButton->setToolTip(enabledButton->isChecked()
		? tr("Switch this line off. It stays in the file as a comment.")
		: tr("Switch this line back on."));
	nameButtonByToolTip(enabledButton);
}

void FilterCardRow::syncSummaryVisibility()
{
	// An open comment card shows its note in the body editor right below, so
	// the header would only say it twice; a closed one keeps the note as its
	// one readout.
	// In a row the summary is the content of a body-less line, and gives way
	// to the raw editor; a row with an editor has no place for it.
	if (inlineRow)
	{
		// A row never folds from its own controls, but a fold from code (the
		// gates) closes its editor, and the summary then stands in for it
		// as on a body-less line.
		summaryLabel->setVisible((!hasEditor || !expanded) && !rawEditing());
		return;
	}
	summaryLabel->setVisible(!(expanded && descriptor.kind == FilterCardKind::Comment));
}

void FilterCardRow::syncVisualState()
{
	if (cardFrame == nullptr)
		return;

	visualInfo = currentRowInfo();
	cardFrame->applyRowInfo(visualInfo, headerWidget);
}
