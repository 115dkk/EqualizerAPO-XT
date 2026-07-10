#include "FilterCardEditorFactory.h"

#include <QSet>
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
	const QString normalizedCommand = command.trimmed().toLower();

	// A line with inline `expression` parameters has its numbers decided at
	// load time. An editor that parses and re-serializes would read garbage
	// and a single interaction would rewrite the line without the
	// expression, so only editors with a dynamic mode may open; everything
	// else stands down and the line falls to the legacy chain, where the
	// Expression GUI factory blanks it into the raw body - a guard the
	// card-first lookup would otherwise bypass.
	if (FilterCardModel::hasInlineExpressions(parameters))
	{
		static const QSet<QString> dynamicCapable = {
			QStringLiteral("preamp"), QStringLiteral("delay")
		};
		if (!dynamicCapable.contains(normalizedCommand))
			return nullptr;
	}

	FilterCardEditorCreator creator = FilterCardEditorRegistry::find(normalizedCommand);
	if (creator == nullptr)
		return nullptr;
	return creator(filterTable, command, parameters);
}
