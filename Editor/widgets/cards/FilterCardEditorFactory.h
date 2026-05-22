#pragma once

class IFilterGUI;
class FilterTable;
class QString;

namespace FilterCardEditorFactory
{
	IFilterGUI* create(FilterTable* filterTable, const QString& command, const QString& parameters);
}
