#include "FilterCardEditorFactory.h"

#include <QString>

#include "Editor/IFilterGUI.h"
#include "FilterCardEditorRegistry.h"

IFilterGUI* FilterCardEditorFactory::create(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	// The roster lives in the card editors' own translation units via
	// REGISTER_FILTER_CARD_EDITOR (see FilterCardEditorRegistry.h); this
	// function is just the lookup, so adding a card no longer means editing
	// an if-chain here. (audit #146 TD003)
	const QString normalizedCommand = command.trimmed().toLower();
	FilterCardEditorCreator creator = FilterCardEditorRegistry::find(normalizedCommand);
	if (creator == nullptr)
		return nullptr;
	return creator(filterTable, command, parameters);
}
