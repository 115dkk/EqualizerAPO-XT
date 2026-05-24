#include "SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QStringList>
#include <QWidget>

SkinManager::SkinManager(QObject* parent)
	: QObject(parent)
{
	currentTokens = loadTokens(skinId, darkMode);
}

SkinManager* SkinManager::instance()
{
	static SkinManager manager;
	return &manager;
}

const SkinTokens& SkinManager::tokens() const
{
	return currentTokens;
}

QString SkinManager::currentSkinId() const
{
	return skinId;
}

bool SkinManager::isDark() const
{
	return darkMode;
}

void SkinManager::applySkin(const QString& newSkinId, bool dark)
{
	QString resolvedSkinId = newSkinId;
	const QStringList knownSkins = {
		QStringLiteral("studio"),
		QStringLiteral("minimal"),
		QStringLiteral("soft"),
		QStringLiteral("rack"),
		QStringLiteral("matrix"),
		QStringLiteral("glassy"),
		QStringLiteral("industrial")
	};
	if (!knownSkins.contains(resolvedSkinId))
		resolvedSkinId = QStringLiteral("studio");
	if (resolvedSkinId == QStringLiteral("glassy"))
		resolvedSkinId = QStringLiteral("studio");
	else if (resolvedSkinId == QStringLiteral("industrial"))
		resolvedSkinId = QStringLiteral("rack");

	skinId = resolvedSkinId;
	darkMode = dark;
	currentTokens = loadTokens(skinId, darkMode);

	QFile file(qssPath(skinId, darkMode));
	if (!file.open(QFile::ReadOnly))
	{
		QFile fallback(qssPath(QStringLiteral("glassy"), darkMode));
		if (fallback.open(QFile::ReadOnly))
			qApp->setStyleSheet(QString::fromUtf8(fallback.readAll()));
		else
			qApp->setStyleSheet(QString());
	}
	else
	{
		qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
	}

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();
}

QString SkinManager::qssPath(const QString& skinId, bool dark) const
{
	if (skinId == QStringLiteral("studio"))
		return QStringLiteral(":/skins/glassy_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	if (skinId == QStringLiteral("rack"))
		return QStringLiteral(":/skins/industrial_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	if (skinId == QStringLiteral("matrix"))
		return QStringLiteral(":/skins/industrial_%1.qss").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
	return QStringLiteral(":/skins/%1_%2.qss").arg(skinId, dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

SkinTokens SkinManager::loadTokens(const QString& skinId, bool dark) const
{
	SkinTokens tokens;
	tokens.accent = QStringLiteral("#3B82F6");

	if (skinId == QStringLiteral("minimal"))
	{
		tokens.borderRadius = 0;
		tokens.rowHeight = 32;
		tokens.channelGroupIndent = 16;
		tokens.fontFamily = QStringLiteral("Consolas");
		tokens.monoFontFamily = QStringLiteral("Consolas");
		tokens.channelGroupStyle = SkinTokens::TreeLines;
		tokens.badgeStyle = SkinTokens::OutlineOnly;
		tokens.zebraStripe = true;
		if (dark)
		{
			tokens.background = QStringLiteral("#191919");
			tokens.surface = QStringLiteral("#1f1f1f");
			tokens.card = QStringLiteral("#262626");
			tokens.cardHover = QStringLiteral("#2c2c2c");
			tokens.cardSelected = QStringLiteral("#1f3554");
			tokens.text = QStringLiteral("#cccccc");
			tokens.mutedText = QStringLiteral("#777777");
			tokens.border = QStringLiteral("#3c3c3c");
			tokens.graph = QStringLiteral("#0e0e0e");
			tokens.graphGridMajor = QStringLiteral("#383838");
			tokens.graphGridMinor = QStringLiteral("#2c2c2c");
		}
		else
		{
			tokens.background = QStringLiteral("#F6F6F3");
			tokens.surface = QStringLiteral("#FFFFFF");
			tokens.card = QStringLiteral("#FFFFFF");
			tokens.cardHover = QStringLiteral("#F0F0EC");
			tokens.cardSelected = QStringLiteral("#E8F1FF");
			tokens.text = QStringLiteral("#202020");
			tokens.mutedText = QStringLiteral("#666660");
			tokens.border = QStringLiteral("#D2D2CC");
			tokens.graph = QStringLiteral("#FFFFFF");
			tokens.graphGridMajor = QStringLiteral("#D2D2CC");
			tokens.graphGridMinor = QStringLiteral("#E6E6E0");
		}
	}
	else if (skinId == QStringLiteral("industrial"))
	{
		tokens.borderRadius = 2;
		tokens.rowHeight = 30;
		tokens.channelGroupIndent = 14;
		tokens.channelGroupStyle = SkinTokens::DottedLine;
		tokens.badgeStyle = SkinTokens::WireframeBorder;
		if (dark)
		{
			tokens.background = QStringLiteral("#08080c");
			tokens.surface = QStringLiteral("#0e0e14");
			tokens.card = QStringLiteral("#151520");
			tokens.cardHover = QStringLiteral("#1a1a28");
			tokens.text = QStringLiteral("#b0b0c8");
			tokens.mutedText = QStringLiteral("#606078");
			tokens.border = QStringLiteral("#2a2a3a");
			tokens.graph = QStringLiteral("#050508");
		}
		else
		{
			tokens.background = QStringLiteral("#e0e0e8");
			tokens.surface = QStringLiteral("#d4d4de");
			tokens.card = QStringLiteral("#ecece4");
			tokens.cardHover = QStringLiteral("#f0f0f8");
			tokens.text = QStringLiteral("#1a1a28");
			tokens.mutedText = QStringLiteral("#5a5a70");
			tokens.border = QStringLiteral("#b0b0c0");
			tokens.graph = QStringLiteral("#f2f2f8");
		}
	}
	else if (skinId == QStringLiteral("soft"))
	{
		tokens.borderRadius = 18;
		tokens.rowHeight = 44;
		tokens.channelGroupIndent = 20;
		tokens.density = 2;
		tokens.channelGroupStyle = SkinTokens::SoftShadow;
		tokens.badgeStyle = SkinTokens::SoftPill;
		if (dark)
		{
			tokens.background = QStringLiteral("#171923");
			tokens.surface = QStringLiteral("#202433");
			tokens.card = QStringLiteral("#282D3E");
			tokens.cardHover = QStringLiteral("#30364A");
			tokens.cardSelected = QStringLiteral("#344065");
			tokens.text = QStringLiteral("#F2F4FA");
			tokens.mutedText = QStringLiteral("#A7AEC2");
			tokens.border = QStringLiteral("#3A4056");
			tokens.graph = QStringLiteral("#151925");
		}
		else
		{
			tokens.background = QStringLiteral("#F7F4EF");
			tokens.surface = QStringLiteral("#FFFDF9");
			tokens.card = QStringLiteral("#FFFFFF");
			tokens.cardHover = QStringLiteral("#FFF7EC");
			tokens.cardSelected = QStringLiteral("#EEF2FF");
			tokens.text = QStringLiteral("#28231F");
			tokens.mutedText = QStringLiteral("#786F67");
			tokens.border = QStringLiteral("#E9DED1");
			tokens.graph = QStringLiteral("#FFFAF3");
		}
	}
	else if (skinId == QStringLiteral("rack"))
	{
		tokens.borderRadius = 6;
		tokens.rowHeight = 36;
		tokens.channelGroupIndent = 16;
		tokens.channelGroupStyle = SkinTokens::DottedLine;
		tokens.badgeStyle = SkinTokens::WireframeBorder;
		tokens.accent = dark ? QStringLiteral("#F4B860") : QStringLiteral("#B66A00");
		tokens.accent2 = dark ? QStringLiteral("#5ED0A0") : QStringLiteral("#177A55");
		if (dark)
		{
			tokens.background = QStringLiteral("#0B0D0F");
			tokens.surface = QStringLiteral("#14181C");
			tokens.card = QStringLiteral("#1D2328");
			tokens.cardHover = QStringLiteral("#252B2F");
			tokens.cardSelected = QStringLiteral("#332718");
			tokens.text = QStringLiteral("#E6E0D4");
			tokens.mutedText = QStringLiteral("#9A9488");
			tokens.border = QStringLiteral("#3A4248");
			tokens.graph = QStringLiteral("#060807");
			tokens.graphGridMinor = QStringLiteral("#1F3A31");
		}
		else
		{
			tokens.background = QStringLiteral("#E7E2D8");
			tokens.surface = QStringLiteral("#F4EFE5");
			tokens.card = QStringLiteral("#FFFAEF");
			tokens.cardHover = QStringLiteral("#F7EEDC");
			tokens.cardSelected = QStringLiteral("#FCE8BD");
			tokens.text = QStringLiteral("#2B2721");
			tokens.mutedText = QStringLiteral("#746A5D");
			tokens.border = QStringLiteral("#C9BFAE");
			tokens.graph = QStringLiteral("#FFF7E6");
			tokens.graphGridMinor = QStringLiteral("#D6C4A6");
		}
	}
	else if (skinId == QStringLiteral("matrix"))
	{
		tokens.borderRadius = 10;
		tokens.rowHeight = 38;
		tokens.channelGroupIndent = 22;
		tokens.channelGroupStyle = SkinTokens::GradientBar;
		tokens.badgeStyle = SkinTokens::OutlineOnly;
		tokens.accent = dark ? QStringLiteral("#22D3EE") : QStringLiteral("#008EAA");
		tokens.accent2 = dark ? QStringLiteral("#7CFFB2") : QStringLiteral("#0A8F57");
		if (dark)
		{
			tokens.background = QStringLiteral("#060B10");
			tokens.surface = QStringLiteral("#0B141C");
			tokens.card = QStringLiteral("#101B25");
			tokens.cardHover = QStringLiteral("#142432");
			tokens.cardSelected = QStringLiteral("#082B34");
			tokens.text = QStringLiteral("#DFF5FF");
			tokens.mutedText = QStringLiteral("#7FA0AE");
			tokens.border = QStringLiteral("#233443");
			tokens.graph = QStringLiteral("#041018");
			tokens.graphGridMinor = QStringLiteral("#183443");
		}
		else
		{
			tokens.background = QStringLiteral("#F0F6F8");
			tokens.surface = QStringLiteral("#FFFFFF");
			tokens.card = QStringLiteral("#F9FCFD");
			tokens.cardHover = QStringLiteral("#EDF7FA");
			tokens.cardSelected = QStringLiteral("#D7F8FF");
			tokens.text = QStringLiteral("#10242F");
			tokens.mutedText = QStringLiteral("#5F7782");
			tokens.border = QStringLiteral("#D4E2E8");
			tokens.graph = QStringLiteral("#F9FCFD");
			tokens.graphGridMinor = QStringLiteral("#D4E2E8");
		}
	}
	else
	{
		tokens.borderRadius = 16;
		tokens.rowHeight = 40;
		tokens.channelGroupIndent = 18;
		tokens.channelGroupStyle = SkinTokens::GradientBar;
		tokens.badgeStyle = SkinTokens::ColorPill;
		if (dark)
		{
			tokens.background = QStringLiteral("#070A12");
			tokens.surface = QStringLiteral("#0D1322");
			tokens.card = QStringLiteral("#121A2C");
			tokens.cardHover = QStringLiteral("#182238");
			tokens.cardSelected = QStringLiteral("#1E3158");
			tokens.text = QStringLiteral("#E8EEFB");
			tokens.mutedText = QStringLiteral("#91A0BA");
			tokens.border = QStringLiteral("#26324A");
			tokens.graph = QStringLiteral("#060914");
			tokens.graphGridMinor = QStringLiteral("#26324A");
			tokens.accent = QStringLiteral("#5B8CFF");
			tokens.accent2 = QStringLiteral("#A66CFF");
		}
		else
		{
			tokens.background = QStringLiteral("#EEF2F8");
			tokens.surface = QStringLiteral("#F8FAFE");
			tokens.card = QStringLiteral("#FFFFFF");
			tokens.cardHover = QStringLiteral("#F3F6FC");
			tokens.cardSelected = QStringLiteral("#DDE8FF");
			tokens.text = QStringLiteral("#182033");
			tokens.mutedText = QStringLiteral("#66728A");
			tokens.border = QStringLiteral("#D8E0EF");
			tokens.graph = QStringLiteral("#F6F7FB");
			tokens.graphGridMinor = QStringLiteral("#D8E0EF");
			tokens.accent = QStringLiteral("#2F6BFF");
			tokens.accent2 = QStringLiteral("#8A4DFF");
		}
	}

	tokens.surfaceRaised = tokens.cardHover;
	tokens.surfaceSunken = tokens.graph;
	tokens.graphGridMajor = tokens.border;
	if (tokens.graphGridMinor.isEmpty())
		tokens.graphGridMinor = tokens.border;
	tokens.focusRing = tokens.accent;

	return tokens;
}
