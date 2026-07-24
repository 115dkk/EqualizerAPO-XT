#pragma once

#include <QSettings>
#include <QString>

namespace EditorSettings
{
namespace Keys
{
inline constexpr char Skin[] = "interface/skin";
inline constexpr char Dark[] = "interface/dark";
inline constexpr char LegacyRows[] = "interface/legacyRows";
inline constexpr char NativeTitleBar[] = "interface/nativeTitleBar";
inline constexpr char KnobGainRange[] = "interface/knobGainRange";
}

struct SkinChoice
{
	QString id;
	bool dark = false;
};

inline SkinChoice readSkinChoice(const QSettings& settings, bool defaultDark)
{
	return {
		settings.value(QLatin1String(Keys::Skin), QStringLiteral("studio")).toString(),
		settings.value(QLatin1String(Keys::Dark), defaultDark).toBool()
	};
}

inline void writeSkinChoice(QSettings& settings, const SkinChoice& choice)
{
	settings.setValue(QLatin1String(Keys::Skin), choice.id);
	settings.setValue(QLatin1String(Keys::Dark), choice.dark);
}
}
