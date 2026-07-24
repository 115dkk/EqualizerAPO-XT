#include "EditableValueText.h"

bool parseEditableValueText(const QString& text, const QLocale& fallbackLocale, double* value)
{
	bool ok = false;
	double parsed = QLocale::c().toDouble(text, &ok);
	if (!ok)
		parsed = fallbackLocale.toDouble(text, &ok);
	if (ok && value != nullptr)
		*value = parsed;
	return ok;
}
