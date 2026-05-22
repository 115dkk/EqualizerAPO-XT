#include "SkinManager.h"

#include <QApplication>
#include <QFile>

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
	skinId = newSkinId;
	darkMode = dark;
	currentTokens = loadTokens(skinId, darkMode);

	QFile file(qssPath(skinId, darkMode));
	if (file.open(QFile::ReadOnly))
		qApp->setStyleSheet(QString::fromUtf8(file.readAll()));

	emit skinChanged(currentTokens);
}

QString SkinManager::qssPath(const QString& skinId, bool dark) const
{
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
			tokens.text = QStringLiteral("#cccccc");
			tokens.mutedText = QStringLiteral("#777777");
			tokens.border = QStringLiteral("#3c3c3c");
			tokens.graph = QStringLiteral("#0e0e0e");
		}
		else
		{
			tokens.background = QStringLiteral("#f0f0f0");
			tokens.surface = QStringLiteral("#e6e6e6");
			tokens.card = QStringLiteral("#fafafa");
			tokens.cardHover = QStringLiteral("#f0f0f0");
			tokens.text = QStringLiteral("#1a1a1a");
			tokens.mutedText = QStringLiteral("#666666");
			tokens.border = QStringLiteral("#c0c0c0");
			tokens.graph = QStringLiteral("#ffffff");
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
		tokens.channelGroupStyle = SkinTokens::SoftShadow;
		tokens.badgeStyle = SkinTokens::SoftPill;
		if (dark)
		{
			tokens.background = QStringLiteral("#161620");
			tokens.surface = QStringLiteral("#1e1e2a");
			tokens.card = QStringLiteral("#262636");
			tokens.cardHover = QStringLiteral("#2c2c3e");
			tokens.text = QStringLiteral("#eeeefc");
			tokens.mutedText = QStringLiteral("#9090ac");
			tokens.border = QStringLiteral("#34344a");
			tokens.graph = QStringLiteral("#101018");
		}
		else
		{
			tokens.background = QStringLiteral("#f7f7fa");
			tokens.surface = QStringLiteral("#efeff4");
			tokens.card = QStringLiteral("#ffffff");
			tokens.cardHover = QStringLiteral("#f8f8fc");
			tokens.text = QStringLiteral("#1c1c2c");
			tokens.mutedText = QStringLiteral("#707088");
			tokens.border = QStringLiteral("#e2e2ec");
			tokens.graph = QStringLiteral("#fefefe");
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
			tokens.background = QStringLiteral("#0c0c16");
			tokens.surface = QStringLiteral("#12121e");
			tokens.card = QStringLiteral("#1a1a28");
			tokens.cardHover = QStringLiteral("#222236");
			tokens.text = QStringLiteral("#e8e8f4");
			tokens.mutedText = QStringLiteral("#8888a8");
			tokens.border = QStringLiteral("#2a2a3c");
			tokens.graph = QStringLiteral("#08080f");
		}
		else
		{
			tokens.background = QStringLiteral("#eef0f6");
			tokens.surface = QStringLiteral("#e4e7ee");
			tokens.card = QStringLiteral("#ffffff");
			tokens.cardHover = QStringLiteral("#f6f7fb");
			tokens.text = QStringLiteral("#1a1a2e");
			tokens.mutedText = QStringLiteral("#6a6a8a");
			tokens.border = QStringLiteral("#d7d9e4");
			tokens.graph = QStringLiteral("#f6f7fb");
		}
	}

	return tokens;
}
