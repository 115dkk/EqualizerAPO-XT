#include "FilterCardEditorFactory.h"

#include <QString>
#include <unordered_map>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "ChannelCardEditor.h"
#include "ConvolutionCardEditor.h"
#include "MultiConvolutionCardEditor.h"
#include "DeviceCardEditor.h"
#include "IncludeCardEditor.h"
#include "PreampCardEditor.h"
#include "StageCardEditor.h"
#include "VSTCardEditor.h"
#include "filters/ConvolutionCommand.h"
#include "filters/MultiConvolutionCommand.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/MemoryHelper.h"

IFilterGUI* FilterCardEditorFactory::create(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	const QString normalizedCommand = command.trimmed().toLower();
	if (normalizedCommand == QStringLiteral("preamp"))
		return new PreampCardEditor(PreampCardEditor::parseGain(parameters));
	if (normalizedCommand == QStringLiteral("channel"))
		return new ChannelCardEditor(parameters);
	if (normalizedCommand == QStringLiteral("device"))
		return new DeviceCardEditor(filterTable, parameters);
	if (normalizedCommand == QStringLiteral("stage"))
		return new StageCardEditor(parameters);
	if (normalizedCommand == QStringLiteral("include"))
		return new IncludeCardEditor(filterTable, parameters);
	if (normalizedCommand == QStringLiteral("convolution"))
	{
		// ConvolutionCommand owns the line grammar; the path it yields preserves
		// the author's quotes/variables so store() round-trips the config text.
		ConvolutionCommand cmd;
		ConvolutionCommand::parse(command.trimmed().toStdWString(), parameters.toStdWString(), cmd);
		return new ConvolutionCardEditor(filterTable, QString::fromStdWString(cmd.path));
	}
	if (normalizedCommand == QStringLiteral("multiconvolution"))
	{
		// MultiConvolutionCommand owns the line grammar (mapping and simple
		// forms); the card hosts the mapping in the skin's routing view.
		MultiConvolutionCommand cmd;
		MultiConvolutionCommand::parse(command.trimmed().toStdWString(), parameters.toStdWString(), cmd);
		return new MultiConvolutionCardEditor(filterTable, cmd.mappings, QString::fromStdWString(cmd.path));
	}
	if (normalizedCommand == QStringLiteral("vstplugin"))
	{
		// Parse the line into the engine's VST filter (no plugin DLL is loaded
		// for configPath == L""), then hand the opaque state to the card editor.
		// The store()/parse round-trip is verified lossless (--selftest-vst).
		VSTPluginFilterFactory factory;
		std::wstring commandWStr = L"VSTPlugin";
		std::wstring paramWStr = parameters.toStdWString();
		std::vector<IFilter*> filters = factory.createFilter(L"", commandWStr, paramWStr);
		VSTCardEditor* editor;
		if (!filters.empty())
		{
			VSTPluginFilter* filter = static_cast<VSTPluginFilter*>(filters[0]);
			editor = new VSTCardEditor(filter->getLibrary(), filter->getChunkData(), filter->getParamMap());
		}
		else
		{
			editor = new VSTCardEditor(VSTPluginLibrary::getInstance(L""), L"", std::unordered_map<std::wstring, float>());
		}
		for (IFilter* f : filters)
		{
			f->~IFilter();
			MemoryHelper::free(f);
		}
		return editor;
	}

	return nullptr;
}
