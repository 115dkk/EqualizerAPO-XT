#include "FilterCardEditorFactory.h"

#include <QString>

#include "Editor/IFilterGUI.h"
#include "IncludeCardEditor.h"
#include "PreampCardEditor.h"

IFilterGUI* FilterCardEditorFactory::create(FilterTable* filterTable, const QString& command, const QString& parameters)
{
	const QString normalizedCommand = command.trimmed().toLower();
	if (normalizedCommand == QStringLiteral("preamp"))
		return new PreampCardEditor(PreampCardEditor::parseGain(parameters));
	if (normalizedCommand == QStringLiteral("include"))
		return new IncludeCardEditor(filterTable, parameters);

	return nullptr;
}
