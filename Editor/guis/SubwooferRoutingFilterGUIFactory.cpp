#include "SubwooferRoutingFilterGUIFactory.h"

#include <memory>

#include "SubwooferRouting/StateCodec.h"
#include "devices/AbstractAPOInfo.h"
#include "Editor/FilterGUIFactoryRegistry.h"
#include "Editor/FilterTable.h"
#include "Editor/widgets/cards/SubwooferRoutingCardEditor.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"
#include "audio/ChannelHelper.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::SubwooferRouting,
	SubwooferRoutingFilterGUIFactory)

namespace
{
QString encodedState(const subroute::SubwooferRoutingState& state)
{
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(state);
	if (!encoded.succeeded())
		return QString();

	return QString::fromUtf8(encoded.text->data(),
		static_cast<int>(encoded.text->size()));
}
}

void SubwooferRoutingFilterGUIFactory::initialize(FilterTable* table)
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
SubwooferRoutingFilterGUIFactory::createFilterTemplates()
{
	subroute::SubwooferRoutingState state =
		subwooferroutingeditor::buildDefaultState(deviceChannels);
	QString json = encodedState(state);
	if (json.isEmpty())
	{
		state = subwooferroutingeditor::buildDefaultState(
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

void SubwooferRoutingFilterGUIFactory::startOfFile(
	const QString& path)
{
	configPath = path;
}

IFilterGUI* SubwooferRoutingFilterGUIFactory::createFilterGUI(
	QString& command, QString& parameters)
{
	if (command != QStringLiteral("SubwooferRouting"))
		return nullptr;

	SubwooferRoutingCommand parsed;
	QString parseError;
	if (parameters.trimmed().isEmpty())
	{
		parsed.form = SubwooferRoutingCommand::Form::State;
	}
	else
	{
		std::wstring error;
		if (!SubwooferRoutingCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error))
		{
			parseError = QString::fromStdWString(error);
		}
	}

	return new SubwooferRoutingCardEditor(filterTable, parsed, configPath,
		sampleRate, parameters, parseError);
}
