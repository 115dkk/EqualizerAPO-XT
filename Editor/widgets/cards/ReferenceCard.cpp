/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See ReferenceCard.h for the rationale and the AR2 findings this implements.
*/

#include "ReferenceCard.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Re-evaluate a widget's stylesheet so a freshly changed dynamic property
// (refMissing, severity, ...) takes effect on the next paint.
void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

QString iconColorFor(bool missing, const SkinTokens& tokens)
{
	return missing ? tokens.warning : tokens.mutedText;
}
}

ReferenceCard::ReferenceCard(const QString& kind, QWidget* parent)
	: QWidget(parent), kind(kind)
{
	const QString cap = kind == QStringLiteral("vst") ? QStringLiteral("Vst") : QStringLiteral("Include");

	setObjectName(kind + QStringLiteral("RefCard"));
	setProperty("refCard", true);
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	// --- Display card (default mode) -------------------------------------
	displayWidget = new QWidget(this);
	displayWidget->setObjectName(cap + QStringLiteral("RefDisplay"));
	displayWidget->setAttribute(Qt::WA_StyledBackground, true);
	QHBoxLayout* displayLayout = new QHBoxLayout(displayWidget);
	displayLayout->setContentsMargins(0, 0, 0, 0);
	displayLayout->setSpacing(10);

	iconLabel = new QLabel(displayWidget);
	iconLabel->setObjectName(cap + QStringLiteral("RefIcon"));
	iconLabel->setProperty("refCard", true);
	displayLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

	QWidget* textBlock = new QWidget(displayWidget);
	textBlock->setObjectName(cap + QStringLiteral("RefTextBlock"));
	QVBoxLayout* textLayout = new QVBoxLayout(textBlock);
	textLayout->setContentsMargins(0, 0, 0, 0);
	textLayout->setSpacing(2);

	// Name row: primary name label + the format/abs/missing badges.
	QWidget* nameRow = new QWidget(textBlock);
	QHBoxLayout* nameLayout = new QHBoxLayout(nameRow);
	nameLayout->setContentsMargins(0, 0, 0, 0);
	nameLayout->setSpacing(6);

	nameLabel = new QLabel(nameRow);
	nameLabel->setObjectName(cap + QStringLiteral("RefName"));
	nameLabel->setProperty("refCard", true);
	nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	nameLabel->installEventFilter(this);
	nameLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

	formatBadge = new QLabel(nameRow);
	formatBadge->setObjectName(cap + QStringLiteral("FormatBadge"));
	formatBadge->setAttribute(Qt::WA_StyledBackground, true);
	formatBadge->setVisible(false);
	nameLayout->addWidget(formatBadge, 0, Qt::AlignVCenter);

	absBadge = new QLabel(QStringLiteral("ABS"), nameRow);
	absBadge->setObjectName(cap + QStringLiteral("RefAbsBadge"));
	absBadge->setAttribute(Qt::WA_StyledBackground, true);
	absBadge->setToolTip(tr("Absolute path - this reference is not portable between machines"));
	absBadge->setVisible(false);
	nameLayout->addWidget(absBadge, 0, Qt::AlignVCenter);

	missingBadge = new QLabel(tr("MISSING"), nameRow);
	missingBadge->setObjectName(QStringLiteral("RefMissingBadge"));
	missingBadge->setAttribute(Qt::WA_StyledBackground, true);
	missingBadge->setVisible(false);
	nameLayout->addWidget(missingBadge, 0, Qt::AlignVCenter);

	nameLayout->addStretch(1);
	textLayout->addWidget(nameRow);

	dirLabel = new QLabel(textBlock);
	dirLabel->setObjectName(cap + QStringLiteral("RefDir"));
	dirLabel->setProperty("refCard", true);
	dirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	textLayout->addWidget(dirLabel);

	statusLabel = new QLabel(textBlock);
	statusLabel->setObjectName(cap + QStringLiteral("RefStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	textLayout->addWidget(statusLabel);

	displayLayout->addWidget(textBlock, 1);

	// The host's actions (Locate, ...) land here; the pencil edit button is
	// always present so the path can be edited inline.
	actionLayout = new QHBoxLayout();
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(4);
	displayLayout->addLayout(actionLayout);

	editButton = new QToolButton(displayWidget);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/pencil.svg"),
		QColor(SkinManager::instance()->tokens().text), 18));
	editButton->setToolTip(tr("Edit the path directly"));
	connect(editButton, &QToolButton::clicked, this, [this]() { emit editRequested(); });
	actionLayout->addWidget(editButton, 0, Qt::AlignTop);

	layout->addWidget(displayWidget, 1);

	// --- Inline path editor (edit mode) ----------------------------------
	pathEdit = new QLineEdit(this);
	pathEdit->setObjectName(cap + QStringLiteral("RefPathEdit"));
	pathEdit->setVisible(false);
	connect(pathEdit, &QLineEdit::editingFinished, this, &ReferenceCard::onEditCommitted);
	layout->addWidget(pathEdit, 1);

	applyNeutralStyle();
}

void ReferenceCard::applyNeutralStyle()
{
	const SkinTokens& tk = SkinManager::instance()->tokens();

	// Primary name: emphasised, with a subtle accent when it is the click
	// affordance. The clickable underline is delivered through QSS state.
	nameLabel->setStyleSheet(QStringLiteral(
		"QLabel { color:%1; font-weight:600; }"
		"QLabel[clickable=\"true\"] { color:%2; }"
		"QLabel[refMissing=\"true\"] { color:%3; }")
		.arg(tk.text, tk.accent, tk.warning));

	// Secondary directory metadata: muted, monospace, smaller.
	dirLabel->setStyleSheet(QStringLiteral("color:%1; font-family:\"%2\"; font-size:8pt;")
		.arg(tk.mutedText, tk.monoFontFamily));

	// Status line, severity-coloured.
	statusLabel->setStyleSheet(QStringLiteral(
		"QLabel { color:%1; font-size:8pt; }"
		"QLabel[severity=\"warning\"] { color:%2; }"
		"QLabel[severity=\"critical\"] { color:%3; }")
		.arg(tk.mutedText, tk.warning, tk.danger));

	// Format badge (VST2/VST3): neutral outlined pill, like DeviceFormatBadge.
	formatBadge->setStyleSheet(QStringLiteral(
		"QLabel { color:%1; border:1px solid %2; border-radius:3px; padding:0 5px; font-size:7pt; font-weight:700; }")
		.arg(tk.mutedText, tk.border));

	// Absolute-path badge: warning-tinted so the portability hazard stands out
	// (X-10).
	absBadge->setStyleSheet(QStringLiteral(
		"QLabel { color:%1; border:1px solid %1; border-radius:3px; padding:0 5px; font-size:7pt; font-weight:700; }")
		.arg(tk.warning));

	// Missing badge: danger-filled, the loudest state token (X-3).
	missingBadge->setStyleSheet(QStringLiteral(
		"QLabel { color:%1; background:%2; border-radius:3px; padding:0 5px; font-size:7pt; font-weight:700; }")
		.arg(tk.background, tk.danger));

	// Inline path editor: matches the sunken input look the cards used before.
	pathEdit->setStyleSheet(QStringLiteral(
		"QLineEdit { background:%1; color:%2; border:1px solid %3; border-radius:4px; padding:4px 8px; font-family:\"%4\"; }"
		"QLineEdit:focus { border:1px solid %5; }")
		.arg(tk.surfaceSunken, tk.text, tk.border, tk.monoFontFamily, tk.accent));
}

QToolButton* ReferenceCard::addActionButton(const QString& objectName)
{
	QToolButton* button = new QToolButton(displayWidget);
	button->setObjectName(objectName);
	// Keep the pencil edit button last in the action area.
	actionLayout->insertWidget(actionLayout->indexOf(editButton), button, 0, Qt::AlignTop);
	return button;
}

void ReferenceCard::addActionWidget(QWidget* widget)
{
	widget->setParent(displayWidget);
	actionLayout->insertWidget(actionLayout->indexOf(editButton), widget, 0, Qt::AlignVCenter);
}

void ReferenceCard::setState(const ReferenceCardState& state)
{
	lastState = state;

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	iconLabel->setPixmap(GUIHelper::tintedIcon(
		state.missing
			? QStringLiteral(":/icons/modern/alert-triangle.svg")
			: (kind == QStringLiteral("vst")
				? QStringLiteral(":/icons/modern/plugin.svg")
				: QStringLiteral(":/icons/modern/file-include.svg")),
		QColor(iconColorFor(state.missing, tokens)), 20).pixmap(GUIHelper::scale(QSize(20, 20))));

	nameLabel->setText(state.name);
	nameLabel->setToolTip(state.nameTooltip.isEmpty() ? state.fullPath : state.nameTooltip);
	// The name acts as the open/jump affordance only when the reference resolves.
	const bool clickable = state.nameClickable && !state.missing;
	nameLabel->setProperty("clickable", clickable);
	nameLabel->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);

	dirLabel->setVisible(!state.directory.isEmpty());
	dirLabel->setText(elide(dirLabel, state.directory));
	dirLabel->setToolTip(state.directory);

	formatBadge->setVisible(!state.formatBadge.isEmpty());
	formatBadge->setText(state.formatBadge);

	absBadge->setVisible(state.absolutePath && !state.missing);
	missingBadge->setVisible(state.missing);

	statusLabel->setVisible(!state.statusText.isEmpty());
	statusLabel->setText(state.statusText);
	statusLabel->setProperty("severity",
		state.statusSeverity == ReferenceCardState::Severity::Critical ? QStringLiteral("critical")
		: state.statusSeverity == ReferenceCardState::Severity::Warning ? QStringLiteral("warning")
		: QStringLiteral("none"));

	// Drive the whole card's broken-reference state so a skin's QSS can restyle
	// the frame, icon and badges as one unit (X-3: missing is a row state, not a
	// caption). unpolish/polish makes the property change take effect.
	for (QWidget* widget : { static_cast<QWidget*>(this), static_cast<QWidget*>(displayWidget),
			static_cast<QWidget*>(iconLabel), static_cast<QWidget*>(nameLabel) })
	{
		widget->setProperty("refMissing", state.missing);
		repolish(widget);
	}
	repolish(statusLabel);

	if (pathEdit->text() != state.fullPath)
		pathEdit->setText(state.fullPath);
}

void ReferenceCard::enterEditMode()
{
	if (editing)
		return;
	editing = true;
	pathEdit->setText(lastState.fullPath);
	displayWidget->setVisible(false);
	pathEdit->setVisible(true);
	pathEdit->setFocus();
	pathEdit->selectAll();
}

void ReferenceCard::leaveEditMode()
{
	if (!editing)
		return;
	editing = false;
	pathEdit->setVisible(false);
	displayWidget->setVisible(true);
}

bool ReferenceCard::isEditing() const
{
	return editing;
}

void ReferenceCard::onEditCommitted()
{
	// editingFinished fires on focus loss too, and leaveEditMode below moves
	// focus; guard against re-entrancy so the host sees one pathEdited per edit.
	if (editCommitting)
		return;
	editCommitting = true;
	const QString newPath = pathEdit->text();
	leaveEditMode();
	emit pathEdited(newPath);
	editCommitting = false;
}

bool ReferenceCard::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == nameLabel && event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (mouseEvent->button() == Qt::LeftButton && lastState.nameClickable && !lastState.missing)
		{
			emit nameActivated();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

QString ReferenceCard::elide(const QLabel* label, const QString& text)
{
	if (text.isEmpty())
		return text;
	// Middle-elide so the informative tail (the leaf folder) survives when the
	// path is too wide (X-6). Width is read at set time; the card is rebuilt on
	// resize-driven skin events, and the full string stays in the tooltip.
	const int budget = label->width() > 0 ? label->width() : 360;
	return label->fontMetrics().elidedText(text, Qt::ElideMiddle, budget);
}
