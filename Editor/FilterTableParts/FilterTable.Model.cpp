#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QScrollBar>
#include <QToolBar>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QCursor>
#include <QDial>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "MainWindow.h"
#include "SkinManager.h"
#include "FilterTableRow.h"
#include "FilterTableMimeData.h"
#include "guis/ExpressionFilterGUIFactory.h"
#include "guis/CommentFilterGUIFactory.h"
#include "guis/DeviceFilterGUIFactory.h"
#include "guis/ChannelFilterGUIFactory.h"
#include "guis/StageFilterGUIFactory.h"
#include "guis/PreampFilterGUIFactory.h"
#include "guis/BiQuadFilterGUIFactory.h"
#include "guis/CopyFilterGUIFactory.h"
#include "guis/DelayFilterGUIFactory.h"
#include "guis/IncludeFilterGUIFactory.h"
#include "guis/GraphicEQFilterGUIFactory.h"
#include "guis/ConvolutionFilterGUIFactory.h"
#include "guis/VSTPluginFilterGUIFactory.h"
#include "guis/LoudnessCorrectionFilterGUIFactory.h"
#include "Editor/helpers/GUIHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "helpers/RegistryHelper.h"
#include "FilterTable.h"
#include "Editor/widgets/FilterCardRow.h"

using std::list;
using std::max;
using std::min;
using std::move;
using std::replace;
using std::shared_ptr;
using std::string;
using std::vector;
using std::wstring;


vector<wstring> FilterTable::getChannelNames() const
{
	vector<wstring> channelNames;
	if (selectedDevice != nullptr)
		channelNames = ChannelHelper::getChannelNames(selectedDevice->getChannelCount(), selectedChannelMask);

	return channelNames;
}

void FilterTable::propagateChannels()
{
	vector<wstring> channelNames = getChannelNames();

	for (Item* item : model.items())
	{
		if (item->gui != nullptr)
			item->gui->configureChannels(channelNames);
	}
}

QList<QString> FilterTable::getLines()
{
	return model.lines();
}

void FilterTable::setLines(const QString& configPath, const QList<QString>& lines)
{
	this->configPath = configPath;

	// The document reset (including moving focus/anchor to the first line)
	// lives in the model; the per-file GUI preference restore below stays
	// here because it reads the registry. (audit #146 TD032)
	model.setLines(lines);

	QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
	settings.beginGroup(QString(configPath).replace('\\', '|'));
	QVariant prefsValue = settings.value("rowPrefs");
	QStringList prefLines;
	if (prefsValue.isValid())
		prefLines = prefsValue.toStringList();
	for (QString prefLine : prefLines)
	{
		int index = prefLine.indexOf(':');
		int lineNumber = 0;
		if (index == -1)
			continue;
		lineNumber = prefLine.left(index).toInt();

		if (lineNumber > 0)
		{
			int index2 = prefLine.indexOf(':', index + 1);
			if (index2 != -1)
			{
				QString prefCommand = prefLine.mid(index + 1, index2 - index - 1);
				QString prefString = prefLine.mid(index2 + 1);

				if (lineNumber <= model.items().size())
				{
					Item* item = model.items()[lineNumber - 1];

					QString command;
					int index = item->text.indexOf(':');
					if (index != -1)
						command = item->text.left(index).trimmed();

					if (command == prefCommand)
						item->prefs = QJsonDocument::fromJson(prefString.toUtf8()).toVariant().toMap();
				}
			}
		}
	}
	setScrollOffsets(settings.value("scrollX", 0).toInt(), settings.value("scrollY", 0).toInt());
	settings.endGroup();

	// A document load/replace starts a fresh history: undo must never step
	// back into the previously opened file's contents. (TD049)
	undoHistory.reset(model.lines());

	updateGuis();
}

void FilterTable::commitToHistory()
{
	if (restoringHistory)
		return;

	undoHistory.commit(model.lines());
}

bool FilterTable::canUndo() const
{
	return undoHistory.canUndo();
}

bool FilterTable::canRedo() const
{
	return undoHistory.canRedo();
}

void FilterTable::undo()
{
	if (!undoHistory.canUndo())
		return;

	applyHistoryState(undoHistory.undo());
}

void FilterTable::redo()
{
	if (!undoHistory.canRedo())
		return;

	applyHistoryState(undoHistory.redo());
}

void FilterTable::applyHistoryState(const QList<QString>& lines)
{
	// Full-document replacement, like a paste over everything: per-row GUI
	// prefs (expanded state etc.) reset with the rebuilt rows. linesChanged
	// still fires so the tab dirty flag and the instant-mode save see the
	// restored document, but restoringHistory keeps commitToHistory from
	// recording the replay as a new step.
	model.setLines(lines);
	updateGuis();

	restoringHistory = true;
	emit linesChanged();
	restoringHistory = false;
}

FilterTable::Item* FilterTable::addLine(const QString& line, FilterTable::Item* before)
{
	Item* newItem = model.addLine(line, before);

	emit linesChanged();

	return newItem;
}

FilterTable::Item* FilterTable::itemAfter(FilterTable::Item* item) const
{
	const qsizetype index = model.items().indexOf(item);
	if (index < 0 || index + 1 >= model.items().count())
		return nullptr;
	return model.items().at(index + 1);
}

void FilterTable::removeItem(FilterTable::Item* item)
{
	// The removal and the selection/focus repair live in the model; the signal
	// stays a widget concern. (audit #146 TD032)
	if (!model.removeItem(item))
		return;

	emit linesChanged();
}

QMenu* FilterTable::createAddPopupMenu()
{
	QHash<QList<QString>, QMenu*> pathMap;
	QMenu* rootMenu = new QMenu;
	pathMap[QStringList()] = rootMenu;

	for (IFilterGUIFactory* f : factories)
	{
		QList<FilterTemplate> templates = f->createFilterTemplates();
		for (FilterTemplate t : templates)
		{
			QMenu* menu = pathMap.value(t.getPath());
			if (menu == nullptr)
			{
				QMenu* parentMenu = rootMenu;
				QStringList currentPath;
				for (QString pathSegment : t.getPath())
				{
					currentPath.append(pathSegment);
					menu = pathMap.value(currentPath);
					if (menu == nullptr)
					{
						menu = new QMenu(pathSegment);
						pathMap.insert(currentPath, menu);
						parentMenu->addMenu(menu);
					}
					parentMenu = menu;
				}
			}

			QAction* action = menu->addAction(t.getName());
			action->setData(QVariant::fromValue(t));
		}
	}

	return rootMenu;
}

QList<FilterTemplate> FilterTable::pickerFilterTemplates() const
{
	QList<FilterTemplate> templates;
	for (IFilterGUIFactory* factory : factories)
		templates.append(factory->createFilterTemplates());

	// Several factories share one category name (the Expression factory's
	// If/Eval templates and the Include/Device/Channel/Stage factories all
	// file under "Control"), so the flat factory order interleaves sections.
	// Pickers that print a header per contiguous run would show the same
	// section twice; group the templates by category in first-seen order, the
	// same merge the QMenu path map performs in createAddPopupMenu. The
	// stable sort keeps the factory order within each category.
	QList<QStringList> sectionOrder;
	for (const FilterTemplate& filterTemplate : templates)
		if (!sectionOrder.contains(filterTemplate.getPath()))
			sectionOrder.append(filterTemplate.getPath());
	std::stable_sort(templates.begin(), templates.end(),
		[&sectionOrder](const FilterTemplate& a, const FilterTemplate& b) {
		return sectionOrder.indexOf(a.getPath()) < sectionOrder.indexOf(b.getPath());
	});
	return templates;
}

QList<FilterPickerEntry> FilterTable::filterPickerEntries() const
{
	QList<FilterPickerEntry> entries;
	const QList<FilterTemplate> templates = pickerFilterTemplates();
	for (const FilterTemplate& filterTemplate : templates)
		entries.append({ filterTemplate.getPath(), filterTemplate.getName(), filterTemplate.getLine() });
	return entries;
}

namespace
{
// Dropdown-style host for the skin's picker view: a frameless Qt::Popup that
// closes on outside clicks and Esc like any combo box popup. hideEvent is the
// single funnel for "the popup went away", whatever the reason.
class FilterPickerPopup : public QWidget
{
public:
	explicit FilterPickerPopup(QWidget* parent)
		: QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
	{
	}

	std::function<void()> onHide;

protected:
	void hideEvent(QHideEvent* event) override
	{
		QWidget::hideEvent(event);
		if (onHide)
			onHide();
	}
};
}

bool FilterTable::chooseFilterTemplate(FilterTemplate* selectedTemplate, const QPoint& globalPos)
{
	if (selectedTemplate == nullptr)
		return false;

	// The same grouped ordering the picker view displays - entryChosen hands
	// back an index into this list.
	QList<FilterTemplate> templates = pickerFilterTemplates();
	if (templates.isEmpty())
		return false;

	// The picker itself comes from the active skin so the control matches the
	// skin's design language; this host only provides dropdown behaviour.
	FilterPickerPopup popup(this);
	popup.setObjectName(QStringLiteral("FilterPickerPopup"));
	popup.setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout* layout = new QVBoxLayout(&popup);
	layout->setContentsMargins(0, 0, 0, 0);
	FilterPickerView* view = SkinManager::instance()->createFilterPicker(&popup);
	view->setEntries(filterPickerEntries());
	layout->addWidget(view);

	int chosenIndex = -1;
	QEventLoop loop;
	connect(view, &FilterPickerView::entryChosen, &loop, [&](int index) {
		chosenIndex = index;
		loop.quit();
	});
	connect(view, &FilterPickerView::dismissed, &loop, &QEventLoop::quit);
	popup.onHide = [&loop]() {
		loop.quit();
	};

	popup.adjustSize();
	QPoint position = globalPos.isNull() ? QCursor::pos() : globalPos;
	if (QScreen* screen = QGuiApplication::screenAt(position))
	{
		const QRect available = screen->availableGeometry();
		position.setX(qBound(available.left(), position.x(), available.right() - popup.width() + 1));
		position.setY(qBound(available.top(), position.y(), available.bottom() - popup.height() + 1));
	}
	popup.move(position);
	popup.show();
	view->setFocus();
	loop.exec();
	popup.onHide = nullptr;

	if (chosenIndex < 0 || chosenIndex >= templates.size())
		return false;

	*selectedTemplate = templates[chosenIndex];
	return true;
}


void FilterTable::updateModel()
{
	emit linesChanged();
}

void FilterTable::updateChannels()
{
	propagateChannels();
}

int FilterTable::getPreferredWidth()
{
	if (scrollArea == nullptr)
		return width();

	// Use the scroll area's own outer width minus the vertical scrollbar - this
	// is the actual on-screen visible area and is NOT affected by the inner
	// widget's possibly-inflated size. Both viewport()->width() and
	// maximumViewportSize() follow the inner widget when it grows past the
	// window, which creates a feedback loop: a once-inflated card width
	// permanently inflates the preferred width returned here, which in turn
	// keeps the cards inflated.
	int width = scrollArea->width();
	if (QScrollBar* vBar = scrollArea->verticalScrollBar())
		if (vBar->isVisible())
			width -= vBar->sizeHint().width();
	// Account for QScrollArea's own frame (default 1px each side).
	width -= 2 * scrollArea->frameWidth();
	return qMax(0, width);
}

void FilterTable::updateSizeHints()
{
	// gridLayout is briefly null between clearRows() and updateGuis(): a skin
	// switch (MainWindow::skinSelected) and the dark-mode toggle tear every tab's
	// rows down BEFORE qApp->setStyleSheet for performance, and that stylesheet
	// swap re-lays-out the window, delivering a scrollArea Resize through this
	// object's event filter while the layout is gone. Dereferencing it here then
	// faults (the minimal/soft/rack skin-switch crash, and the intermittent
	// crash on a plain restyle). Nothing to size while the rows are being rebuilt.
	if (gridLayout == nullptr)
		return;
	for (int i = 0; i < model.items().size(); i++)
	{
		QLayoutItem* layoutItem = gridLayout->itemAtPosition(i, 0);
		if (layoutItem == nullptr)
			continue;
		QWidget* rowWidget = layoutItem->widget();
		if (rowWidget != nullptr)
			rowWidget->updateGeometry();
	}
}

QSize FilterTable::minimumSizeHint() const
{
	QSize size = QWidget::minimumSizeHint();
	if (size.height() < minimumHeightHint)
		size.setHeight(minimumHeightHint);

	return size;
}

void FilterTable::setMinimumHeightHint(int height)
{
	minimumHeightHint = height;
	updateGeometry();
}

void FilterTable::setRenderMode(RenderMode mode)
{
	if (renderMode == mode)
		return;

	renderMode = mode;
	updateGuis();
}

FilterTable::RenderMode FilterTable::getRenderMode() const
{
	return renderMode;
}

