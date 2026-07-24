#include "FilterCardEditorFactory.h"

#include <QString>

#include "Editor/IFilterGUI.h"
#include "Editor/widgets/FilterCardModel.h"
#include "FilterCardEditorRegistry.h"

IFilterGUI* FilterCardEditorFactory::create(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	// The roster lives in the card editors' own translation units via
	// REGISTER_FILTER_CARD_EDITOR (see FilterCardEditorRegistry.h); this
	// function is just the lookup, so adding a card does not mean editing
	// an if-chain here.
	//
	// The key is resolved by the engine's rule, not by a private one: that is
	// what lets "Filter 1:" (REW's and Dirac's default spelling) reach the same
	// entry as "Filter:", and what keeps a lowercase "preamp:" - inert prose to
	// the engine - out of the live Preamp card.
	const QString commandKeyword = FilterCardModel::canonicalCommand(command);

	// A line with inline `expression` parameters has its numbers decided at
	// load time. An editor that parses and re-serializes would read garbage
	// and a single interaction would rewrite the line without the
	// expression, so only editors with a dynamic mode may open; everything
	// else stands down and the line falls to the legacy chain, where the
	// Expression GUI factory blanks it into the raw body - a guard the
	// card-first lookup would otherwise bypass.
	if (FilterCardModel::hasInlineExpressions(parameters))
	{
		if (!FilterCardEditorRegistry::supportsDynamicParameters(commandKeyword))
			return nullptr;
	}

	FilterCardEditorCreator creator = FilterCardEditorRegistry::find(commandKeyword);
	if (creator == nullptr)
		return nullptr;
	// The creator still sees the key as written, because the Filter entry is
	// shared: IIRCardEditor hands it to IIRFilterFactory::parseCommand, which is
	// what tells "Filter 1: ON IIR ..." apart from an ordinary BiQuad line and
	// lets the latter fall through to the legacy knob GUI.
	return creator(filterTable, command, parameters);
}
