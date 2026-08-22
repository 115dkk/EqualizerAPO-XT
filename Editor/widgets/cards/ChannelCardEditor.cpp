#include "ChannelCardEditor.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

#include "filters/ChannelCommand.h"

ChannelCardEditor::ChannelCardEditor(const QString& parameters, QWidget* parent)
	: IFilterGUI(parent), parameters(parameters)
{
	setObjectName(QStringLiteral("ChannelCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	allChip = new QToolButton(this);
	allChip->setObjectName(QStringLiteral("ChannelChip"));
	allChip->setText(QStringLiteral("ALL"));
	allChip->setCheckable(true);
	allChip->setToolTip(tr("Select every channel"));
	// Stable QSS handle for the master chip, parallel to the Device card's
	// "allDevices" property; skins style ALL differently from a single seat.
	allChip->setProperty("allChannels", true);
	connect(allChip, SIGNAL(toggled(bool)), this, SLOT(allToggled(bool)));
	layout->addWidget(allChip);

	chipLayout = new QHBoxLayout();
	chipLayout->setContentsMargins(0, 0, 0, 0);
	chipLayout->setSpacing(6);
	layout->addLayout(chipLayout);

	customEdit = new QLineEdit(this);
	customEdit->setObjectName(QStringLiteral("ChannelChipAdd"));
	customEdit->setPlaceholderText(tr("Add channel"));
	customEdit->setToolTip(tr("Add a custom or virtual channel name (e.g. VSL)"));
	customEdit->setMaximumWidth(110);
	connect(customEdit, SIGNAL(returnPressed()), this, SLOT(customEntered()));
	layout->addWidget(customEdit);

	layout->addStretch(1);

	model.load(parameters, deviceChannels);
	reloadChips();
}

void ChannelCardEditor::store(QString& command, QString& storedParameters)
{
	command = QStringLiteral("Channel");
	storedParameters = model.serialize();
}

void ChannelCardEditor::configureChannels(std::vector<std::wstring>& channelNames)
{
	deviceChannels = channelNames;
	// Re-seed the chips with the device's channel set, keeping the current
	// selection (parameters tracks the latest edit).
	model.load(parameters, deviceChannels);
	reloadChips();
}

void ChannelCardEditor::configureSelectedChannels(std::vector<std::wstring>& selectedChannels)
{
	// Replace the flowing selection with this line's, resolved by the same
	// routine the tests pin to ChannelFilter. deviceChannels is the in-scope
	// vector configureChannels just delivered, so Copy-created channels
	// above this row resolve here exactly as they would in the engine.
	ChannelCommand cmd;
	if (!ChannelCommand::parse(L"Channel", parameters.toStdWString(), cmd))
		return;
	selectedChannels = ChannelCommand::resolveSelection(cmd.channels, deviceChannels);
}

void ChannelCardEditor::allToggled(bool checked)
{
	if (updating)
		return;

	model.setAllSelected(checked);
	// ALL wins over individual selections, exactly like the legacy dialog;
	// individual chips stay visible but inert while it is checked. Only the
	// enabled states change here - rebuilding the chip row inside a chip's
	// own toggled signal would delete the emitting button.
	for (int i = 0; i < chipLayout->count(); i++)
	{
		if (QWidget* chip = chipLayout->itemAt(i)->widget())
			chip->setEnabled(!checked);
	}
	customEdit->setEnabled(!checked);
	commitSelection();
}

void ChannelCardEditor::customEntered()
{
	if (!model.addCustom(customEdit->text()))
		return;

	customEdit->clear();
	// Safe to rebuild: the sender is the line edit, not one of the chips.
	reloadChips();
	commitSelection();
}

void ChannelCardEditor::reloadChips()
{
	updating = true;

	while (QLayoutItem* child = chipLayout->takeAt(0))
	{
		delete child->widget();
		delete child;
	}

	const bool all = model.allSelected();
	allChip->setChecked(all);

	for (const ChannelChip& chip : model.chips())
	{
		QToolButton* button = new QToolButton(this);
		button->setObjectName(QStringLiteral("ChannelChip"));
		button->setText(chip.name);
		button->setCheckable(true);
		button->setChecked(chip.selected);
		// Custom/virtual channels are stylable separately (the channel badges
		// use a dashed outline for them; skins may do the same here).
		button->setProperty("customChannel", !chip.fromDevice);
		button->setEnabled(!all);
		const QString name = chip.name;
		connect(button, &QToolButton::toggled, this, [this, name](bool) {
			if (updating)
				return;
			model.toggle(name);
			commitSelection();
		});
		chipLayout->addWidget(button);
	}

	customEdit->setEnabled(!all);

	updating = false;
}

void ChannelCardEditor::commitSelection()
{
	parameters = model.serialize();
	emit updateModel();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Channel, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	return new ChannelCardEditor(parameters);
})
