/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "FilterPickerView.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "Editor/helpers/GUIHelper.h"

QString filterTemplateDescription(const QString& rawLine)
{
	const QString line = rawLine.trimmed();
	if (line.isEmpty())
		return QString();
	if (line.startsWith(QLatin1Char('#')))
		return QCoreApplication::translate("FilterPickerView", "A note EqualizerAPO skips while processing");

	const int colon = line.indexOf(QLatin1Char(':'));
	const QString command = (colon > 0 ? line.left(colon) : line).trimmed();

	// Biquad templates all share the "Filter" command, so split further on the
	// type token to give each response shape its own line. The tokens match the
	// Soft picker's pictogram table (BiQuadFilterGUIFactory writes them).
	if (command == QLatin1String("Filter"))
	{
		static const struct { const char* token; const char* description; } curves[] = {
			{ " PK ", QT_TRANSLATE_NOOP("FilterPickerView", "Boosts or cuts a band around a center frequency") },
			{ " LP ", QT_TRANSLATE_NOOP("FilterPickerView", "Passes the lows and rolls off above the cutoff") },
			{ " HP ", QT_TRANSLATE_NOOP("FilterPickerView", "Passes the highs and rolls off below the cutoff") },
			{ " BP ", QT_TRANSLATE_NOOP("FilterPickerView", "Passes a band around the center and drops the rest") },
			{ " LS ", QT_TRANSLATE_NOOP("FilterPickerView", "Raises or lowers everything below the corner frequency") },
			{ " HS ", QT_TRANSLATE_NOOP("FilterPickerView", "Raises or lowers everything above the corner frequency") },
			{ " NO ", QT_TRANSLATE_NOOP("FilterPickerView", "Cuts a narrow band deeply and leaves the rest") },
			{ " AP ", QT_TRANSLATE_NOOP("FilterPickerView", "Shifts phase around a frequency without changing level") }
		};
		for (const auto& curve : curves)
			if (line.contains(QLatin1String(curve.token)))
				return QCoreApplication::translate("FilterPickerView", curve.description);
		return QString();
	}

	static const struct { const char* command; const char* description; } commands[] = {
		{ "Preamp", QT_TRANSLATE_NOOP("FilterPickerView", "Applies overall gain before the other filters") },
		{ "Delay", QT_TRANSLATE_NOOP("FilterPickerView", "Delays the signal by a time or distance") },
		{ "Copy", QT_TRANSLATE_NOOP("FilterPickerView", "Mixes and routes the signal between channels") },
		{ "GraphicEQ", QT_TRANSLATE_NOOP("FilterPickerView", "Sets a gain for each graphic-EQ band") },
		{ "Convolution", QT_TRANSLATE_NOOP("FilterPickerView", "Applies an impulse response, such as a room or reverb") },
		{ "MultiConvolution", QT_TRANSLATE_NOOP("FilterPickerView", "Convolves several inputs, as in BRIR headphone synthesis") },
		{ "LoudnessCorrection", QT_TRANSLATE_NOOP("FilterPickerView", "Compensates hearing at low listening levels") },
		{ "VSTPlugin", QT_TRANSLATE_NOOP("FilterPickerView", "Runs an external VST audio plugin") },
		{ "Channel", QT_TRANSLATE_NOOP("FilterPickerView", "Selects which channels the following filters affect") },
		{ "Device", QT_TRANSLATE_NOOP("FilterPickerView", "Limits the following filters to one device") },
		{ "Stage", QT_TRANSLATE_NOOP("FilterPickerView", "Chooses the processing stage for the following filters") },
		{ "Include", QT_TRANSLATE_NOOP("FilterPickerView", "Loads another configuration file here") },
		{ "Eval", QT_TRANSLATE_NOOP("FilterPickerView", "Computes a variable from an expression") },
		{ "If", QT_TRANSLATE_NOOP("FilterPickerView", "Applies the following filters only when a condition holds") },
		{ "ElseIf", QT_TRANSLATE_NOOP("FilterPickerView", "Tries another condition when the previous one failed") },
		{ "Else", QT_TRANSLATE_NOOP("FilterPickerView", "Runs when none of the conditions above matched") },
		{ "EndIf", QT_TRANSLATE_NOOP("FilterPickerView", "Closes the conditional block") }
	};
	for (const auto& mapping : commands)
		if (command == QLatin1String(mapping.command))
			return QCoreApplication::translate("FilterPickerView", mapping.description);
	return QString();
}

FilterPickerView::FilterPickerView(QWidget* parent)
	: QWidget(parent)
{
}

void FilterPickerView::galleryShowcase(GalleryShowcase)
{
}

DefaultFilterPickerView::DefaultFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
{
	setObjectName(QStringLiteral("FilterPicker"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	searchEdit = new QLineEdit(this);
	searchEdit->setObjectName(QStringLiteral("FilterPickerSearch"));
	searchEdit->setPlaceholderText(tr("Search filters"));
	searchEdit->setClearButtonEnabled(true);
	// Arrow keys and Return typed in the search field drive the list below, so
	// keyboard users never have to leave the field.
	searchEdit->installEventFilter(this);
	connect(searchEdit, &QLineEdit::textChanged, this, &DefaultFilterPickerView::rebuildList);
	layout->addWidget(searchEdit);

	listWidget = new QListWidget(this);
	listWidget->setObjectName(QStringLiteral("FilterPickerList"));
	listWidget->setFrameShape(QFrame::NoFrame);
	listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	listWidget->setUniformItemSizes(false);
	// Dropdown semantics: one click inserts.
	connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
		if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
			emit entryChosen(item->data(Qt::UserRole).toInt());
	});
	connect(listWidget, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
			emit entryChosen(item->data(Qt::UserRole).toInt());
	});
	layout->addWidget(listWidget, 1);

	setMinimumWidth(GUIHelper::scale(300.0));
	setMaximumHeight(GUIHelper::scale(420.0));
}

void DefaultFilterPickerView::setEntries(const QList<FilterPickerEntry>& entries)
{
	allEntries = entries;
	rebuildList();
	searchEdit->setFocus();
}

void DefaultFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::EmptySearch)
	{
		// A term that matches no template: the gallery captures what the user
		// sees after a fruitless search.
		searchEdit->setText(QStringLiteral("zzzz"));
		return;
	}

	searchEdit->clear();
	for (int row = 0; row < listWidget->count(); row++)
	{
		QListWidgetItem* item = listWidget->item(row);
		if (!(item->flags() & Qt::ItemIsSelectable))
			continue;
		// Hover is driven by real mouse events (the view keeps a hover index
		// updated from MouseMove); feed it a synthetic move over the first
		// entry so the offscreen render shows the hover styling.
		listWidget->viewport()->setAttribute(Qt::WA_UnderMouse, true);
		const QPointF center = listWidget->visualItemRect(item).center();
		QMouseEvent moveEvent(QEvent::MouseMove, center,
			listWidget->viewport()->mapToGlobal(center),
			Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(listWidget->viewport(), &moveEvent);
		listWidget->viewport()->update();
		break;
	}
}

void DefaultFilterPickerView::rebuildList()
{
	listWidget->clear();

	const QStringList terms = searchEdit->text().split(
		QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

	QString currentSection;
	bool sectionStarted = false;
	for (int i = 0; i < allEntries.size(); i++)
	{
		const FilterPickerEntry& entry = allEntries[i];
		const QString section = entry.path.isEmpty() ? tr("General") : entry.path.join(QStringLiteral(" / "));

		bool matches = true;
		const QString haystack = section + QLatin1Char(' ') + entry.name + QLatin1Char(' ') + entry.line;
		for (const QString& term : terms)
		{
			if (!haystack.contains(term, Qt::CaseInsensitive))
			{
				matches = false;
				break;
			}
		}
		if (!matches)
			continue;

		if (!sectionStarted || section != currentSection)
		{
			sectionStarted = true;
			currentSection = section;
			QListWidgetItem* caption = new QListWidgetItem(section, listWidget);
			caption->setFlags(Qt::NoItemFlags);
			QFont captionFont = caption->font();
			captionFont.setBold(true);
			captionFont.setPointSizeF(captionFont.pointSizeF() * 0.9);
			caption->setFont(captionFont);
		}

		QListWidgetItem* item = new QListWidgetItem(entry.name, listWidget);
		item->setData(Qt::UserRole, i);
		item->setToolTip(entry.line);
	}

	// Preselect the first real entry so Return inserts immediately.
	for (int row = 0; row < listWidget->count(); row++)
	{
		if (listWidget->item(row)->flags() & Qt::ItemIsSelectable)
		{
			listWidget->setCurrentRow(row);
			break;
		}
	}
}

void DefaultFilterPickerView::chooseCurrent()
{
	QListWidgetItem* item = listWidget->currentItem();
	if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
		emit entryChosen(item->data(Qt::UserRole).toInt());
}

bool DefaultFilterPickerView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == searchEdit && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key())
		{
		case Qt::Key_Down:
		case Qt::Key_Up:
		case Qt::Key_PageDown:
		case Qt::Key_PageUp:
			QApplication::sendEvent(listWidget, event);
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			chooseCurrent();
			return true;
		default:
			break;
		}
	}
	return FilterPickerView::eventFilter(watched, event);
}
