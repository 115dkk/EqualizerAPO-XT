#include "FilterCardRow.h"

#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#include "Editor/SkinManager.h"
#include "Editor/widgets/ChBadge.h"
#include "Editor/widgets/ElidedLabel.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/CopyRoutingAdapter.h"

namespace
{
// Row types the engine narrows to the enclosing Channel: selection (every
// downstream filter initializes against the selected channel set), so an
// inherited scope badge on them states a fact. Channel/Copy rows carry their
// own channel badges, and control rows (device, stage, eval, the If family),
// notes and unknown raw text are not gated by the selection at all.
bool channelSelectionGatesType(const QString& type)
{
	return type == QStringLiteral("biquad")
		|| type == QStringLiteral("preamp")
		|| type == QStringLiteral("delay")
		|| type == QStringLiteral("graphiceq")
		|| type == QStringLiteral("convolution")
		|| type == QStringLiteral("velvet")
		|| type == QStringLiteral("vst")
		|| type == QStringLiteral("loudness")
		|| type == QStringLiteral("include");
}
}

FilterCardRow::FilterCardRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui,
	FilterCardDescriptor preparedDescriptor, QWidget* parent)
	: QWidget(parent), table(table), item(item), gui(gui), descriptor(std::move(preparedDescriptor)), rowNumber(number)
{
	setAttribute(Qt::WA_StyledBackground, false);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

	QVBoxLayout* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(8 + rowIndentUnits() * SkinManager::instance()->tokens().channelGroupIndent, 4, 8, 4);
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
	cardLayout->addWidget(headerWidget);

	// Dress the frame and header now, while the card is two widgets deep:
	// setStyleSheet() re-resolves the style of every descendant, so applying
	// these sheets to the finished ~40-widget card (the applyDescriptor() call
	// at the end of this constructor used to be the first apply) re-styled the
	// whole subtree per card - measured ~3 ms each on a 300-row config.
	// Children created below inherit the sheets as they appear, and the
	// applyDescriptor() re-run finds identical strings and properties, so it
	// skips both the re-set and the construction-time repolish entirely.
	refreshStateProperties();

	QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
	headerLayout->setContentsMargins(8, 4, 8, 4);
	headerLayout->setSpacing(8);

	expandButton = new QToolButton(headerWidget);
	expandButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	expandButton->setCheckable(true);
	expandButton->setChecked(gui != nullptr);
	expandButton->setText(expandButton->isChecked() ? QStringLiteral("v") : QStringLiteral(">"));
	expandButton->setToolTip(tr("Expand filter card"));
	connect(expandButton, SIGNAL(toggled(bool)), this, SLOT(expandedToggled(bool)));
	headerLayout->addWidget(expandButton);

	numberLabel = new QLabel(QString::number(number), headerWidget);
	numberLabel->setObjectName(QStringLiteral("FilterCardNumber"));
	numberLabel->setAlignment(Qt::AlignCenter);
	numberLabel->setMinimumWidth(28);
	headerLayout->addWidget(numberLabel);

	typeBadge = new QLabel(headerWidget);
	typeBadge->setObjectName(QStringLiteral("FilterTypeBadge"));
	typeBadge->setAlignment(Qt::AlignCenter);
	typeBadge->setMinimumWidth(46);
	headerLayout->addWidget(typeBadge);

	titleLabel = new ElidedLabel(headerWidget);
	titleLabel->setObjectName(QStringLiteral("FilterCardTitle"));
	titleLabel->setElideMode(Qt::ElideRight);
	titleLabel->setMinimumWidth(92);
	titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	headerLayout->addWidget(titleLabel);

	summaryLabel = new ElidedLabel(headerWidget);
	summaryLabel->setObjectName(QStringLiteral("FilterCardSummary"));
	summaryLabel->setElideMode(Qt::ElideRight);
	summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	summaryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	headerLayout->addWidget(summaryLabel, 1);

	channelBadgeContainer = new QWidget(headerWidget);
	// A plain QWidget matches every skin's global "QWidget { background: @BG@ }"
	// rule, and the style then paints that app-background plate over the card
	// surface - a dark rectangle cut around the badges. The widget-level rule
	// outranks any skin sheet, so the strip stays transparent under all skins.
	channelBadgeContainer->setStyleSheet(QStringLiteral("background: transparent;"));
	channelBadgeLayout = new QHBoxLayout(channelBadgeContainer);
	channelBadgeLayout->setContentsMargins(0, 0, 0, 0);
	channelBadgeLayout->setSpacing(3);
	channelBadgeContainer->setVisible(false);
	headerLayout->addWidget(channelBadgeContainer);

	enabledButton = new QToolButton(headerWidget);
	enabledButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	enabledButton->setCheckable(true);
	enabledButton->setToolTip(tr("Enable or comment out this command"));
	enabledButton->setChecked(descriptor.enabled);
	enabledButton->setIcon(QIcon(descriptor.enabled ? QStringLiteral(":/icons/power_on.svg") : QStringLiteral(":/icons/power_off.svg")));
	connect(enabledButton, SIGNAL(toggled(bool)), this, SLOT(enabledToggled(bool)));
	headerLayout->addWidget(enabledButton);

	addButton = new QToolButton(headerWidget);
	addButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	addButton->setText(QStringLiteral("+"));
	addButton->setToolTip(tr("Add filter above this card"));
	connect(addButton, SIGNAL(clicked()), this, SLOT(addAbove()));
	headerLayout->addWidget(addButton);

	removeButton = new QToolButton(headerWidget);
	removeButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	removeButton->setText(QStringLiteral("-"));
	removeButton->setToolTip(tr("Remove filter"));
	connect(removeButton, SIGNAL(clicked()), this, SLOT(removeThis()));
	headerLayout->addWidget(removeButton);

	editButton = new QToolButton(headerWidget);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setCheckable(true);
	editButton->setText(QStringLiteral("..."));
	editButton->setToolTip(tr("Edit raw command"));
	connect(editButton, SIGNAL(toggled(bool)), this, SLOT(editTextToggled(bool)));
	headerLayout->addWidget(editButton);

	rawPreviewLabel = new QLabel(cardFrame);
	rawPreviewLabel->setObjectName(QStringLiteral("FilterCardRawPreview"));
	rawPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	cardLayout->addWidget(rawPreviewLabel);

	bodyStack = new QStackedWidget(cardFrame);
	bodyStack->setObjectName(QStringLiteral("FilterCardBody"));
	bodyStack->setAttribute(Qt::WA_StyledBackground, true);
	// Stop an overgrown body editor (e.g. the legacy DeviceFilterGUI's
	// QTreeWidget with sizeAdjustPolicy=AdjustToContents, or any other GUI that
	// reports a content-driven sizeHint) from propagating its preferred width
	// up through the card. Otherwise the card grows past the visible viewport
	// and the right-side header toolbar (enable / + / - / ...) renders
	// thousands of pixels off screen and becomes invisible.
	// Ignored sizePolicy alone is not enough: it stops sizeHint propagation
	// but the layout system still inherits the inner widgets' minimumSize.
	// Pin the bodyStack's own minimumSize to 0 so the card frame's minimum
	// width is driven by the header only.
	bodyStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	bodyStack->setMinimumSize(0, 0);
	cardLayout->addWidget(bodyStack);

	lineEdit = new QLineEdit(bodyStack);
	lineEdit->setObjectName(QStringLiteral("FilterCardRawEditor"));
	connect(lineEdit, SIGNAL(editingFinished()), this, SLOT(lineEditingFinished()));
	bodyStack->addWidget(lineEdit);

	// A Copy line with inline `expression` factors must not open the routing
	// view: its parser would read the unresolved text as garbage and the
	// first edit would serialize the expression away. Such lines keep the
	// raw body (dynamic-value contract), like every other dynamic line
	// without a dynamic-capable editor.
	IRoutingRenderer* routingRenderer = (descriptor.type == QStringLiteral("copy")
		&& !descriptor.dynamicLine)
		? SkinManager::instance()->routingRenderer() : nullptr;

	if (routingRenderer != nullptr)
	{
		// Skin-specific Copy routing view (crosspoint matrix, step list, ...).
		// The view owns its working routing state; on edit we serialise it
		// back into item->text.
		QWidget* editorContainer = new QWidget(bodyStack);
		editorContainer->setObjectName(QStringLiteral("FilterCardEditor"));
		editorContainer->setAttribute(Qt::WA_StyledBackground, true);
		editorContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		editorContainer->setMinimumSize(0, 0);
		QVBoxLayout* editorLayout = new QVBoxLayout(editorContainer);
		editorLayout->setContentsMargins(12, 10, 12, 12);

		std::vector<Assignment> routingAssignments = CopyRoutingAdapter::parse(descriptor.parameters);
		// Seed the routing editor with the real device channel set (L, R, C, ...),
		// the same list the legacy CopyFilterGUI receives via configureChannels.
		// Without it the graph only shows channels already named in the line, so
		// the user cannot route to/from a channel that has no assignment yet
		// (e.g. copying L onto R when R is not referenced).
		std::vector<std::wstring> channelNames = table->getChannelNames();
		// Copy uses the default port model: symmetric sources/targets seeded
		// from the device channels, with editable factors.
		routingView = routingRenderer->create(routingAssignments, channelNames, RoutingPortModel(), editorContainer);

		QScrollArea* routingScroll = new QScrollArea(editorContainer);
		routingScroll->setObjectName(QStringLiteral("FilterCardEditorScroll"));
		routingScroll->setFrameShape(QFrame::NoFrame);
		routingScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		routingScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		routingScroll->setWidgetResizable(true);
		routingScroll->setMinimumSize(0, 0);
		routingScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		routingScroll->setWidget(routingView);
		watchEditorScroll(routingScroll);
		editorLayout->addWidget(routingScroll);

		bodyStack->addWidget(editorContainer);
		bodyStack->setCurrentWidget(editorContainer);
		connect(routingView, SIGNAL(routingChanged()), this, SLOT(routingEdited()));
		connect(routingView, SIGNAL(routingChanged()), table, SLOT(updateChannels()));

		// The expand default above keys off the gui the body shows; routing
		// rows are their own editor and start open.
		expandButton->setChecked(true);
	}
	else if (gui != nullptr)
	{
		QWidget* editorContainer = new QWidget(bodyStack);
		editorContainer->setObjectName(QStringLiteral("FilterCardEditor"));
		editorContainer->setAttribute(Qt::WA_StyledBackground, true);
		// Match bodyStack: stop overgrown filter GUIs from inflating the card width.
		editorContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		editorContainer->setMinimumSize(0, 0);
		QVBoxLayout* editorLayout = new QVBoxLayout(editorContainer);
		editorLayout->setContentsMargins(12, 10, 12, 12);
		// Wrap the legacy filter GUI in a borderless scroll area. Without this,
		// any filter GUI that has a content-driven sizeHint (DeviceFilterGUI's
		// QTreeWidget AdjustToContents, GraphicEQ / Convolution / VSTPlugin's
		// internal AdjustToContents widgets) propagates a huge minimumSize up
		// through editorContainer/bodyStack/cardFrame/FilterCardRow and the
		// QGridLayout in FilterTable then forces every card column to that
		// width, pushing the right-side header toolbar (enable / + / - / ...)
		// thousands of pixels off screen.
		QWidget* guiWidget = qobject_cast<QWidget*>(gui);
		if (guiWidget != nullptr)
		{
			QScrollArea* guiScroll = new QScrollArea(editorContainer);
			guiScroll->setObjectName(QStringLiteral("FilterCardEditorScroll"));
			guiScroll->setFrameShape(QFrame::NoFrame);
			guiScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
			guiScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			guiScroll->setWidgetResizable(true);
			guiScroll->setMinimumSize(0, 0);
			guiScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
			guiScroll->setWidget(guiWidget);
			watchEditorScroll(guiScroll);
			editorLayout->addWidget(guiScroll);
		}
		else
		{
			editorLayout->addWidget(gui);
		}
		bodyStack->addWidget(editorContainer);
		bodyStack->setCurrentWidget(editorContainer);
		connect(gui, SIGNAL(updateModel()), this, SLOT(updateModel()));
	}
	else
	{
		// Unrecognized command line: no dedicated editor. Present a styled
		// monospace raw card with the command token emphasized, so it reads
		// as a deliberate raw row instead of bare plain text.
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

	bodyStack->setVisible(expandButton->isChecked());
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		refreshStateProperties();
		update();
	});
	// Per-command-type chrome hook: rows are recreated on every skin switch
	// (FilterTable::updateGuis), so construction time is the right moment for
	// the active skin to tag or extend this row.
	SkinManager::instance()->prepareCommandRow(currentRowInfo(), cardFrame, headerWidget, bodyStack);
	applyDescriptor();
}

void FilterCardRow::configureChannels(std::vector<std::wstring>& channelNames)
{
	if (routingView != nullptr && descriptor.type == QStringLiteral("copy"))
	{
		if (descriptor.enabled)
			propagateCopyChannels(CopyRoutingAdapter::parse(descriptor.parameters), channelNames);
		return;
	}

	if (gui != nullptr)
		gui->configureChannels(channelNames);
}

CommandRowInfo FilterCardRow::currentRowInfo() const
{
	CommandRowInfo info;
	info.type = descriptor.type;
	info.command = descriptor.command.toLower();
	info.enabled = descriptor.enabled;
	info.selected = table != nullptr && table->getSelectedItems().contains(item);
	info.focused = table != nullptr && table->getFocusedItem() == item;
	info.depth = descriptor.depth;
	info.logicDepth = descriptor.logicDepth;
	info.dynamicLine = descriptor.dynamicLine;

	// The lane geometry, resolved here because this widget is what decides it:
	// rowIndentUnits() is the same call its own layout margin uses, so a skin's
	// gutter can no longer disagree with the card face it sits beside.
	info.laneUnit = SkinManager::instance()->tokens().channelGroupIndent;
	info.laneCount = rowIndentUnits();
	info.cardLeft = 8 + info.laneCount * info.laneUnit;

	// Fold in the analysis engine's load facts for this line (dynamic
	// commands): branch truth for the If family, computed values for
	// Eval/inline expressions, and whether a false branch swallowed the line.
	// Advisory by contract - facts go stale between an edit and the next
	// analysis run. When both an Eval result and an inline substitution exist
	// on one line, the Eval result wins regardless of hash order.
	if (table != nullptr)
	{
		const QList<ConfigLoadTraceEntry> facts = table->loadTraceFactsForRow(rowNumber - 1);
		for (const ConfigLoadTraceEntry& fact : facts)
		{
			switch (fact.kind)
			{
			case ConfigLoadTraceEntry::Kind::Condition:
				info.branchState = fact.result == ConfigLoadTraceEntry::Result::True ? 1
					: fact.result == ConfigLoadTraceEntry::Result::False ? 0
					: fact.result == ConfigLoadTraceEntry::Result::Error ? 3 : 2;
				break;
			case ConfigLoadTraceEntry::Kind::ElseBranch:
				info.branchState = fact.active ? 1 : 0;
				break;
			case ConfigLoadTraceEntry::Kind::Eval:
				info.evalText = QString::fromStdWString(fact.text);
				info.valueError = info.valueError || fact.error;
				break;
			case ConfigLoadTraceEntry::Kind::InlineValue:
				// The engine preserves the space after the colon; trim at
				// this UI boundary so readouts do not start with a gap.
				if (info.evalText.isEmpty())
					info.evalText = QString::fromStdWString(fact.text).trimmed();
				info.valueError = info.valueError || fact.error;
				break;
			case ConfigLoadTraceEntry::Kind::SkippedLine:
				info.lineSkipped = true;
				break;
			case ConfigLoadTraceEntry::Kind::ParseError:
				// Several can arrive for one line if a factory reports more than
				// once; the first is the one that stopped it.
				if (info.parseError.isEmpty())
					info.parseError = QString::fromStdWString(fact.text);
				break;
			}
		}
	}
	return info;
}

QRect FilterCardRow::getHeaderRect() const
{
	return QRect(headerWidget->pos(), headerWidget->size());
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
			layout()->setContentsMargins(8 + rowIndentUnits() * SkinManager::instance()->tokens().channelGroupIndent, 4, 8, 4);
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
	if (descriptor.type == QStringLiteral("spacer"))
	{
		int preferredWidth = table != nullptr ? table->getPreferredWidth() : 0;
		return QSize(preferredWidth, 10);
	}

	// Use only the header's preferred width and the viewport - never the body's
	// content-driven sizeHint. Some legacy filter GUIs (DeviceFilterGUI with
	// QTreeWidget AdjustToContents, VST / Convolution / Stage editors) report
	// a sizeHint matching their full content (thousands of pixels), which
	// would otherwise inflate the entire QGridLayout column in FilterTable and
	// push the right-side header toolbar far off screen.
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
	if (descriptor.type == QStringLiteral("spacer"))
		return QSize(0, 10);

	// Cap the cell's minimum width at a small constant so the QGridLayout in
	// FilterTable does not inherit any content-driven minimum from a card's
	// body editor. Without this, ONE row with a wide legacy filter GUI forces
	// EVERY card column wide enough to push the right-side header toolbar far
	// off screen. Height stays layout-driven so cards still vertically size to
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
void FilterCardRow::watchEditorScroll(QScrollArea* scroll)
{
	scroll->installEventFilter(this);
	if (scroll->widget() != nullptr)
		scroll->widget()->installEventFilter(this);
	syncEditorScrollHeight(scroll);
}

void FilterCardRow::syncEditorScrollHeight(QScrollArea* scroll)
{
	QWidget* content = scroll->widget();
	if (content == nullptr)
		return;

	const int width = scroll->viewport()->width();
	int desired = content->hasHeightForWidth() && width > 0
		? content->heightForWidth(width)
		: content->sizeHint().height();
	// Generous cap: legacy filter GUIs can report content heights of
	// thousands of pixels (the same hints the width clamp exists for); a
	// runaway body must not swallow the whole table.
	desired = qBound(24, desired, 600);
	if (scroll->minimumHeight() != desired || scroll->maximumHeight() != desired)
	{
		scroll->setFixedHeight(desired);
		// The new height must reach the FilterTable grid, but the layout
		// chain above the scroll (editor container -> body stack -> card
		// frame -> row) re-validates lazily hop by hop and the cascade can
		// stall with a stale cached minimum mid-chain - observed as "the
		// card never grows" when the routing views' channel fold expands.
		// Invalidate the whole chain explicitly so the next grid pass
		// re-queries a fresh row minimum.
		for (QWidget* w = scroll->parentWidget(); w != nullptr && w != this; w = w->parentWidget())
			if (w->layout() != nullptr)
				w->layout()->invalidate();
		if (layout() != nullptr)
			layout()->invalidate();
		updateGeometry();
	}
}

bool FilterCardRow::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
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

int FilterCardRow::rowIndentUnits() const
{
	// Member level is one unit past the branch/tail row's own semantic level
	// (depth already carries any enclosing channel group, so +1 keeps mixed
	// Channel x If nesting aligned with the block members).
	if (descriptor.type == QStringLiteral("if") && descriptor.badge != QStringLiteral("IF")
		&& SkinManager::instance()->logicSiblingsIndentAsMembers())
		return descriptor.depth + 1;
	return descriptor.depth;
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
	case SkinTokens::SoftShadow:
	{
		QLinearGradient shadow(lineRect.left(), 0, lineRect.right(), 0);
		QColor start = color;
		start.setAlpha(38);
		QColor end = color;
		end.setAlpha(0);
		shadow.setColorAt(0, start);
		shadow.setColorAt(1, end);
		painter.fillRect(lineRect, shadow);
		break;
	}
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
	// describeLine() cannot see neighbouring lines, so the If-scope count is
	// carried over from the previous descriptor (assigned by the constructor /
	// updateRowPosition from calculateScopes).
	const int logicDepth = descriptor.logicDepth;
	const QStringList scopeChannels = descriptor.scopeChannels;
	descriptor = FilterCardModel::describeLine(item->text, descriptor.depth);
	descriptor.logicDepth = logicDepth;
	descriptor.scopeChannels = scopeChannels;
	applyDescriptor();
}

void FilterCardRow::applyDescriptor()
{

	// Blank lines render as a thin spacer: no header, no body, no raw preview.
	// The card frame itself stays visible (so its background fills the gap and
	// scope-rail painting still works for indented blocks) but is collapsed
	// to a small fixed height by sizeHint() / minimumSizeHint() below.
	const bool isSpacer = descriptor.type == QStringLiteral("spacer");
	if (headerWidget != nullptr)
		headerWidget->setVisible(!isSpacer);
	if (bodyStack != nullptr)
		bodyStack->setVisible(!isSpacer && expandButton != nullptr && expandButton->isChecked());
	if (isSpacer)
	{
		if (rawPreviewLabel != nullptr)
			rawPreviewLabel->setVisible(false);
		refreshStateProperties();
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
	titleLabel->setFullText(descriptor.title);
	summaryLabel->setFullText(descriptor.summary);
	// A line the engine could not use says why on hover. The analysis run is what
	// produces the reason, so this is empty until one has happened and goes stale
	// on edit, like every other load fact.
	const QString parseError = currentRowInfo().parseError;
	summaryLabel->setToolTip(parseError.isEmpty() ? descriptor.summary
		: tr("This line was not applied: %1").arg(parseError));
	// The text stays current even while the label is hidden: skins may read
	// it as the live raw-spec source instead of showing the label itself
	// (MatrixRowCaption's caption strip does).
	rawPreviewLabel->setText(tr("Raw") + QStringLiteral("  ") + item->text);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	rawPreviewLabel->setVisible(tokens.showRawPreview);
	// Skins without a raw preview never show the label, and rows are rebuilt
	// on every skin switch - skip the per-widget stylesheet for them.
	if (tokens.showRawPreview)
	{
		const QString previewStyle = QStringLiteral("QLabel#FilterCardRawPreview { background: %1; color: %2; border-top: 1px solid %3; padding: 4px 12px; font-family: \"%4\"; font-size: 9pt; }")
			.arg(tokens.surfaceSunken, tokens.mutedText, tokens.border, tokens.monoFontFamily);
		if (rawPreviewLabel->styleSheet() != previewStyle)
			rawPreviewLabel->setStyleSheet(previewStyle);
	}
	enabledButton->blockSignals(true);
	enabledButton->setChecked(descriptor.enabled);
	enabledButton->setIcon(QIcon(descriptor.enabled ? QStringLiteral(":/icons/power_on.svg") : QStringLiteral(":/icons/power_off.svg")));
	enabledButton->blockSignals(false);
	enabledButton->setVisible(descriptor.canToggleEnabled);
	// Disable only the body editor when the line is commented out. This keeps
	// the card frame (and its header buttons - including the enable toggle and
	// the raw-edit affordance) interactive so the user can flip the line back on.
	// A pure comment row is "disabled" by definition (the line starts with '#'),
	// but its body IS the note editor - keep it editable.
	if (gui != nullptr)
		gui->setEnabled(descriptor.enabled || descriptor.type == QStringLiteral("comment"));
	// A row's own channel list (the Channel card's selection, Copy's
	// destinations) wins. Other rows inside a Channel: selection inherit the
	// selection's badges, so the group's reach is readable on every member
	// row instead of only on its head - but only for row types the engine
	// actually narrows to the selection; control rows, notes and raw text
	// would claim an influence they do not have.
	QStringList badgeChannels = descriptor.channelBadges;
	if (badgeChannels.isEmpty() && channelSelectionGatesType(descriptor.type))
		badgeChannels = descriptor.scopeChannels;
	buildChannelBadges(badgeChannels);
	refreshStateProperties();
	update();
}

void FilterCardRow::buildChannelBadges(const QStringList& channels)
{
	if (channels == renderedChannelBadges)
		return;
	renderedChannelBadges = channels;

	while (QLayoutItem* child = channelBadgeLayout->takeAt(0))
	{
		delete child->widget();
		delete child;
	}

	for (const QString& channel : channels.mid(0, 8))
		channelBadgeLayout->addWidget(new ChBadge(channel, channelBadgeContainer));
	channelBadgeContainer->setVisible(!channels.isEmpty());
}

void FilterCardRow::updateModel()
{
	IFilterGUI* senderGui = qobject_cast<IFilterGUI*>(QObject::sender());
	if (senderGui == nullptr)
		return;

	QString command;
	QString parameters;
	senderGui->store(command, parameters);
	// "#" is the comment card's sentinel: a pure comment line has no colon, so
	// it is reassembled as "# <text>" (a bare "#" when the note is empty).
	if (command == QStringLiteral("#"))
		item->text = parameters.isEmpty() ? QStringLiteral("#") : QStringLiteral("# ") + parameters;
	else
		item->text = command + QStringLiteral(": ") + parameters;
	rebuildSummary();
	table->updateModel();
}

void FilterCardRow::routingEdited()
{
	if (routingView == nullptr)
		return;

	const QString parameters = CopyRoutingAdapter::serialize(routingView->assignments());
	item->text = QStringLiteral("Copy: ") + parameters;
	rebuildSummary();
	table->updateModel();
}

void FilterCardRow::addAbove()
{
	FilterTemplate filterTemplate;
	if (table->chooseFilterTemplate(&filterTemplate, addButton->mapToGlobal(QPoint(0, addButton->height()))))
	{
		// A card header's + belongs to that card's leading edge: addLine takes
		// an insert-before anchor, so this row itself is the desired anchor.
		table->addLine(filterTemplate.getLine(), item);
		FilterTable* targetTable = table;
		QTimer::singleShot(0, targetTable, [targetTable]() {
			targetTable->updateGuis();
		});
	}
}

void FilterCardRow::removeThis()
{
	FilterTable* targetTable = table;
	FilterTable::Item* targetItem = item;
	QTimer::singleShot(0, targetTable, [targetTable, targetItem]() {
		targetTable->removeItem(targetItem);
		targetTable->updateGuis();
	});
}

void FilterCardRow::editTextToggled(bool checked)
{
	setEditing(checked);
}

void FilterCardRow::setEditing(bool editing)
{
	if (editing)
	{
		lineEdit->setText(item->text);
		bodyStack->setCurrentWidget(lineEdit);
		bodyStack->setVisible(true);
		expandButton->setChecked(true);
		lineEdit->setFocus();
		lineEdit->selectAll();
	}
	else if (gui != nullptr)
	{
		bodyStack->setCurrentIndex(1);
	}
	else if (bodyStack->count() > 1)
	{
		bodyStack->setCurrentIndex(1);
	}
}

void FilterCardRow::lineEditingFinished()
{
	if (bodyStack->currentWidget() == lineEdit && !editingDone)
	{
		editingDone = true;
		if (lineEdit->text() != item->text)
		{
			item->text = lineEdit->text();
			table->updateModel();
			editingDone = false;
			FilterTable* targetTable = table;
			QTimer::singleShot(0, targetTable, [targetTable]() {
				targetTable->updateGuis();
			});
			return;
		}
		editButton->setChecked(false);
		editingDone = false;
	}
}

QString FilterCardRow::uncommentedLine() const
{
	QRegularExpression commentPrefix(QStringLiteral("^(\\s*)#\\s?"));
	QRegularExpressionMatch match = commentPrefix.match(item->text);
	if (match.hasMatch())
		return match.captured(1) + item->text.mid(match.capturedEnd(0));
	return item->text;
}

void FilterCardRow::enabledToggled(bool checked)
{
	if (!descriptor.canToggleEnabled)
		return;

	if (enabledButton != nullptr)
		enabledButton->setIcon(QIcon(checked ? QStringLiteral(":/icons/power_on.svg") : QStringLiteral(":/icons/power_off.svg")));

	QString trimmed = item->text.trimmed();
	if (checked && trimmed.startsWith('#'))
		item->text = uncommentedLine();
	else if (!checked && !trimmed.startsWith('#'))
		item->text = QStringLiteral("# ") + item->text;

	table->updateModel();
	FilterTable* targetTable = table;
	FilterTable::Item* targetItem = item;
	// In-place refresh: only the toggled row's GUI needs to change. A full
	// updateGuis() on a 500+ row config is the dominant source of toggle
	// latency.
	QTimer::singleShot(0, targetTable, [targetTable, targetItem]() {
		targetTable->updateSingleRowGui(targetItem);
	});
}

void FilterCardRow::expandedToggled(bool checked)
{
	expandButton->setText(checked ? QStringLiteral("v") : QStringLiteral(">"));
	bodyStack->setVisible(checked);
}

void FilterCardRow::refreshStateProperties()
{
	if (cardFrame == nullptr)
		return;

	visualInfo = currentRowInfo();
	cardFrame->applyRowInfo(visualInfo, headerWidget);
}
