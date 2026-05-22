#include "FilterCardRow.h"

#include <QMenu>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/widgets/ChBadge.h"

FilterCardRow::FilterCardRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui, int depth, QWidget* parent)
	: QWidget(parent), table(table), item(item), gui(gui)
{
	descriptor = FilterCardModel::describeLine(item->text, depth);
	setAttribute(Qt::WA_StyledBackground, false);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

	QVBoxLayout* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(8 + descriptor.depth * SkinManager::instance()->tokens().channelGroupIndent, 4, 8, 4);
	outerLayout->setSpacing(0);

	cardFrame = new QFrame(this);
	cardFrame->setObjectName(QStringLiteral("FilterCardRow"));
	cardFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
	outerLayout->addWidget(cardFrame);

	QVBoxLayout* cardLayout = new QVBoxLayout(cardFrame);
	cardLayout->setContentsMargins(0, 0, 0, 0);
	cardLayout->setSpacing(0);

	headerWidget = new QWidget(cardFrame);
	headerWidget->setObjectName(QStringLiteral("FilterCardHeader"));
	headerWidget->setMinimumHeight(SkinManager::instance()->tokens().rowHeight);
	cardLayout->addWidget(headerWidget);

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

	titleLabel = new QLabel(headerWidget);
	titleLabel->setObjectName(QStringLiteral("FilterCardTitle"));
	titleLabel->setMinimumWidth(92);
	headerLayout->addWidget(titleLabel);

	summaryLabel = new QLabel(headerWidget);
	summaryLabel->setObjectName(QStringLiteral("FilterCardSummary"));
	summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	summaryLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	headerLayout->addWidget(summaryLabel, 1);

	channelBadgeContainer = new QWidget(headerWidget);
	channelBadgeLayout = new QHBoxLayout(channelBadgeContainer);
	channelBadgeLayout->setContentsMargins(0, 0, 0, 0);
	channelBadgeLayout->setSpacing(3);
	headerLayout->addWidget(channelBadgeContainer);

	enabledCheckBox = new QCheckBox(headerWidget);
	enabledCheckBox->setObjectName(QStringLiteral("FilterCardEnabled"));
	enabledCheckBox->setToolTip(tr("Enable or comment out this command"));
	enabledCheckBox->setChecked(descriptor.enabled);
	connect(enabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(enabledToggled(bool)));
	headerLayout->addWidget(enabledCheckBox);

	addButton = new QToolButton(headerWidget);
	addButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	addButton->setText(QStringLiteral("+"));
	addButton->setToolTip(tr("Add filter before this card"));
	connect(addButton, SIGNAL(clicked()), this, SLOT(addBefore()));
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

	bodyStack = new QStackedWidget(cardFrame);
	bodyStack->setObjectName(QStringLiteral("FilterCardBody"));
	cardLayout->addWidget(bodyStack);

	lineEdit = new QLineEdit(bodyStack);
	lineEdit->setObjectName(QStringLiteral("FilterCardRawEditor"));
	connect(lineEdit, SIGNAL(editingFinished()), this, SLOT(lineEditingFinished()));
	bodyStack->addWidget(lineEdit);

	if (gui != nullptr)
	{
		QWidget* editorContainer = new QWidget(bodyStack);
		editorContainer->setObjectName(QStringLiteral("FilterCardEditor"));
		QVBoxLayout* editorLayout = new QVBoxLayout(editorContainer);
		editorLayout->setContentsMargins(12, 10, 12, 12);
		editorLayout->addWidget(gui);
		bodyStack->addWidget(editorContainer);
		bodyStack->setCurrentWidget(editorContainer);
		connect(gui, SIGNAL(updateModel()), this, SLOT(updateModel()));
	}
	else
	{
		QLabel* rawLabel = new QLabel(item->text, bodyStack);
		rawLabel->setObjectName(QStringLiteral("FilterCardRawText"));
		rawLabel->setWordWrap(true);
		rawLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		bodyStack->addWidget(rawLabel);
		bodyStack->setCurrentWidget(rawLabel);
	}

	bodyStack->setVisible(expandButton->isChecked());
	rebuildSummary();
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

QSize FilterCardRow::sizeHint() const
{
	QSize size = QWidget::sizeHint();
	int preferredWidth = table->getPreferredWidth();
	if (size.width() < preferredWidth)
		size.setWidth(preferredWidth);
	return size;
}

void FilterCardRow::paintEvent(QPaintEvent*)
{
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

void FilterCardRow::rebuildSummary()
{
	descriptor = FilterCardModel::describeLine(item->text, descriptor.depth);
	typeBadge->setText(descriptor.badge);
	bool outlineBadge = SkinManager::instance()->tokens().badgeStyle == SkinTokens::OutlineOnly || SkinManager::instance()->tokens().badgeStyle == SkinTokens::WireframeBorder;
	typeBadge->setStyleSheet(QStringLiteral("color:%1; border-color:%2; background-color:%3;")
		.arg(outlineBadge ? descriptor.color : QStringLiteral("white"),
			descriptor.color,
			outlineBadge ? QStringLiteral("transparent") : descriptor.color));
	titleLabel->setText(descriptor.title);
	summaryLabel->setText(descriptor.summary);
	enabledCheckBox->blockSignals(true);
	enabledCheckBox->setChecked(descriptor.enabled);
	enabledCheckBox->blockSignals(false);
	buildChannelBadges(descriptor.channelBadges);
	update();
}

void FilterCardRow::buildChannelBadges(const QStringList& channels)
{
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
	item->text = command + QStringLiteral(": ") + parameters;
	rebuildSummary();
	table->updateModel();
}

void FilterCardRow::addBefore()
{
	QMenu* menu = table->createAddPopupMenu();
	QPoint popupPos = addButton->mapToGlobal(QPoint(0, addButton->height()));
	QAction* action = menu->exec(popupPos);
	if (action != nullptr)
	{
		FilterTemplate filterTemplate = action->data().value<FilterTemplate>();
		table->addLine(filterTemplate.getLine(), item);
		table->updateGuis();
	}
	delete menu;
}

void FilterCardRow::removeThis()
{
	table->removeItem(item);
	table->updateGuis();
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
			table->updateGuis();
			editingDone = false;
			return;
		}
		editButton->setChecked(false);
		editingDone = false;
	}
}

QString FilterCardRow::uncommentedLine() const
{
	QString trimmed = item->text.trimmed();
	if (trimmed.startsWith('#'))
		return trimmed.mid(1).trimmed();
	return item->text;
}

void FilterCardRow::enabledToggled(bool checked)
{
	QString trimmed = item->text.trimmed();
	if (checked && trimmed.startsWith('#'))
		item->text = uncommentedLine();
	else if (!checked && !trimmed.startsWith('#'))
		item->text = QStringLiteral("# ") + item->text;

	table->updateModel();
	table->updateGuis();
}

void FilterCardRow::expandedToggled(bool checked)
{
	expandButton->setText(checked ? QStringLiteral("v") : QStringLiteral(">"));
	bodyStack->setVisible(checked);
}
