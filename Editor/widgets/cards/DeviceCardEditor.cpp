#include "DeviceCardEditor.h"

#include <QToolButton>

#include "Editor/FilterTable.h"
#include "Editor/widgets/FlowLayout.h"
#include <devices/AbstractAPOInfo.h>

using std::shared_ptr;

namespace
{
QList<DeviceEntry> buildEntries(FilterTable* filterTable)
{
	QList<DeviceEntry> entries;
	if (filterTable == nullptr)
		return entries;

	// Output devices then input devices, matching the order the legacy device
	// dialog grouped (Playback then Capture) so the serialized line order stays
	// byte-identical.
	const QList<shared_ptr<AbstractAPOInfo>> groups[] = {
		filterTable->getOutputDevices(),
		filterTable->getInputDevices()
	};
	for (const QList<shared_ptr<AbstractAPOInfo>>& devices : groups)
	{
		for (const shared_ptr<AbstractAPOInfo>& apoInfo : devices)
		{
			DeviceEntry entry;
			entry.deviceString = QString::fromStdWString(apoInfo->getDeviceString());
			entry.name = QString::fromStdWString(apoInfo->getDeviceName());
			entry.connection = QString::fromStdWString(apoInfo->getConnectionName());
			entry.installed = apoInfo->isInstalled();
			entry.isInput = apoInfo->isInput();
			entries.append(entry);
		}
	}
	return entries;
}
}

DeviceCardEditor::DeviceCardEditor(FilterTable* filterTable, const QString& parameters, QWidget* parent)
	: IFilterGUI(parent)
{
	setObjectName(QStringLiteral("DeviceCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	flow = new FlowLayout(this, 0, 6, 6);

	// Persistent controls live at flow indices 0 and 1; reloadChips() only ever
	// rebuilds the device chips after them, so a chip can never delete the
	// control whose signal triggered the rebuild (the Channel card's rule).
	allChip = new QToolButton(this);
	allChip->setObjectName(QStringLiteral("DeviceChip"));
	allChip->setText(tr("All devices"));
	allChip->setCheckable(true);
	allChip->setProperty("allDevices", true);
	allChip->setToolTip(tr("Apply to every device"));
	connect(allChip, SIGNAL(toggled(bool)), this, SLOT(allToggled(bool)));
	flow->addWidget(allChip);

	showAllButton = new QToolButton(this);
	showAllButton->setObjectName(QStringLiteral("DeviceChipMore"));
	showAllButton->setCheckable(true);
	showAllButton->setToolTip(tr("Show devices that do not have the APO installed"));
	connect(showAllButton, SIGNAL(toggled(bool)), this, SLOT(showAllToggled(bool)));
	flow->addWidget(showAllButton);

	model.load(parameters, buildEntries(filterTable));
	reloadChips();
}

void DeviceCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Device");
	parameters = model.serialize();
}

void DeviceCardEditor::allToggled(bool checked)
{
	if (updating)
		return;

	model.setAllSelected(checked);
	// "All devices" wins over individual selections, like the legacy dialog;
	// the device chips stay visible but inert while it is checked. Only enabled
	// states change here - rebuilding the chip row inside a chip's own toggled
	// signal would delete the emitting button.
	for (int i = 2; i < flow->count(); i++)
	{
		if (QWidget* chip = flow->itemAt(i)->widget())
			chip->setEnabled(!checked);
	}
	showAllButton->setEnabled(!checked);
	emit updateModel();
}

void DeviceCardEditor::showAllToggled(bool checked)
{
	if (updating)
		return;

	showAll = checked;
	// Safe to rebuild: the sender is the persistent reveal toggle, not a chip.
	reloadChips();
}

void DeviceCardEditor::reloadChips()
{
	updating = true;

	// Drop the device chips (everything after the two persistent controls),
	// keeping allChip (0) and showAllButton (1).
	while (flow->count() > 2)
	{
		QLayoutItem* child = flow->takeAt(flow->count() - 1);
		delete child->widget();
		delete child;
	}

	const bool all = model.allSelected();
	allChip->setChecked(all);

	hiddenUninstalled = 0;
	for (const DeviceChipInfo& chip : model.chips())
	{
		// By default show installed devices and whatever is currently selected;
		// keep machines with many idle endpoints from filling the card.
		const bool visible = showAll || chip.installed || chip.selected;
		if (!visible)
		{
			hiddenUninstalled++;
			continue;
		}

		QToolButton* button = new QToolButton(this);
		button->setObjectName(QStringLiteral("DeviceChip"));
		button->setText(chip.connection.isEmpty() ? chip.name : chip.connection);
		button->setCheckable(true);
		button->setChecked(chip.selected);
		button->setEnabled(!all);
		// Skins may dim not-installed chips or distinguish capture devices.
		button->setProperty("deviceInstalled", chip.installed);
		button->setProperty("deviceInput", chip.isInput);
		QString tip = chip.connection.isEmpty() ? chip.name : chip.connection + " - " + chip.name;
		tip += "\n" + (chip.isInput ? tr("Capture") : tr("Playback"));
		tip += "\n" + (chip.installed ? tr("APO installed") : tr("APO not installed"));
		button->setToolTip(tip);
		const QString deviceString = chip.deviceString;
		connect(button, &QToolButton::toggled, this, [this, deviceString](bool) {
			if (updating)
				return;
			model.toggle(deviceString);
			emit updateModel();
		});
		flow->addWidget(button);
	}

	// The reveal toggle only matters when some devices are hidden, or while it
	// is already revealing them (so the user can collapse again).
	showAllButton->setVisible(hiddenUninstalled > 0 || showAll);
	showAllButton->setEnabled(!all);
	showAllButton->blockSignals(true);
	showAllButton->setChecked(showAll);
	showAllButton->setText(showAll ? tr("Show fewer") : tr("Show all (+%1)").arg(hiddenUninstalled));
	showAllButton->blockSignals(false);

	updating = false;
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Device, [](FilterTable* filterTable, const QString&, const QString& parameters) -> IFilterGUI* {
	return new DeviceCardEditor(filterTable, parameters);
})
