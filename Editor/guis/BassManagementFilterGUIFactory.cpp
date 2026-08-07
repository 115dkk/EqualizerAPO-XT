#include "BassManagementFilterGUIFactory.h"

#include <memory>

#include "BassManagement/StateCodec.h"
#include "devices/AbstractAPOInfo.h"
#include "Editor/FilterGUIFactoryRegistry.h"
#include "Editor/FilterTable.h"
#include "Editor/widgets/cards/BassManagementCardEditor.h"
#include "filters/bassManagement/BassManagementCommand.h"
#include "helpers/ChannelHelper.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::BassManagement,
	BassManagementFilterGUIFactory)

namespace
{
QString encodedState(const bassmgmt::BassManagementState& state)
{
	const bassmgmt::StateEncodeResult encoded =
		bassmgmt::encodeStateCanonical(state);
	if (!encoded.succeeded())
		return QString();

	return QString::fromUtf8(encoded.text->data(),
		static_cast<int>(encoded.text->size()));
}
}

void BassManagementFilterGUIFactory::initialize(FilterTable* table)
{
	filterTable = table;
	configPath = table == nullptr ? QString() : table->getConfigPath();

	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	const unsigned value = device == nullptr ? 0 : device->getSampleRate();
	sampleRate = value == 0 ? 48000 : value;
	deviceChannels = device == nullptr ? std::vector<std::wstring>()
		: ChannelHelper::getChannelNames(device->getChannelCount(),
			device->getChannelMask());
}

QList<FilterTemplate>
BassManagementFilterGUIFactory::createFilterTemplates()
{
	bassmgmt::BassManagementState state =
		bassmanagementeditor::buildDefaultState(deviceChannels);
	QString json = encodedState(state);
	if (json.isEmpty())
	{
		state = bassmanagementeditor::buildDefaultState(
			std::vector<std::wstring>{L"L", L"R"});
		json = encodedState(state);
	}

	return {
		FilterTemplate(
			tr("Subwoofer routing (crossover + LFE routing)"),
			QStringLiteral("SubwooferRouting: State ") + json,
			QStringList(tr("Speaker management")))
	};
}

void BassManagementFilterGUIFactory::startOfFile(
	const QString& path)
{
	configPath = path;
}

IFilterGUI* BassManagementFilterGUIFactory::createFilterGUI(
	QString& command, QString& parameters)
{
	if (command != QStringLiteral("SubwooferRouting"))
		return nullptr;

	BassManagementCommand parsed;
	QString parseError;
	if (parameters.trimmed().isEmpty())
	{
		parsed.form = BassManagementCommand::Form::State;
	}
	else
	{
		std::wstring error;
		if (!BassManagementCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error))
		{
			parseError = QString::fromStdWString(error);
		}
	}

	return new BassManagementCardEditor(filterTable, parsed, configPath,
		sampleRate, parameters, parseError);
}
