#include "SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QWidget>

#include "skins/ISkin.h"
#include "skins/Skins.h"

SkinManager::SkinManager(QObject* parent)
	: QObject(parent)
{
	activeSkin = Skins::byId(skinId);
	skinId = activeSkin->id();
	currentTokens = activeSkin->tokens(darkMode);
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

const QString& SkinManager::currentSkinId() const
{
	return skinId;
}

bool SkinManager::isDark() const
{
	return darkMode;
}

namespace
{
QString substituteTokens(QString qss, const SkinTokens& tokens)
{
	// Token sentinels intentionally use the @TOKEN@ form so they survive Qt's
	// style sheet parser intact (a literal '@' is not meaningful in QSS) and
	// stand out in the source files. Order does not matter because every
	// sentinel is unique.
	struct Substitution { const char* placeholder = nullptr; QString value; };
	const Substitution table[] = {
		{ "@BG@", tokens.background },
		{ "@SURFACE@", tokens.surface },
		{ "@SURFACE_RAISED@", tokens.surfaceRaised },
		{ "@SURFACE_SUNKEN@", tokens.surfaceSunken },
		{ "@CARD@", tokens.card },
		{ "@CARD_HOVER@", tokens.cardHover },
		{ "@CARD_SELECTED@", tokens.cardSelected },
		{ "@TEXT@", tokens.text },
		{ "@MUTED@", tokens.mutedText },
		{ "@BORDER@", tokens.border },
		{ "@GRAPH@", tokens.graph },
		{ "@GRID_MAJOR@", tokens.graphGridMajor },
		{ "@GRID_MINOR@", tokens.graphGridMinor },
		{ "@ACCENT@", tokens.accent },
		{ "@ACCENT2@", tokens.accent2 },
		{ "@SUCCESS@", tokens.success },
		{ "@WARNING@", tokens.warning },
		{ "@DANGER@", tokens.danger },
		{ "@FOCUS@", tokens.focusRing },
		{ "@FONT@", tokens.fontFamily },
		{ "@MONO@", tokens.monoFontFamily }
	};
	for (const Substitution& s : table)
		qss.replace(QLatin1String(s.placeholder), s.value);
	return qss;
}
}

void SkinManager::applySkin(const QString& newSkinId, bool dark)
{
	// Skins::byId applies legacy aliases (glassy->studio, industrial->rack) and
	// falls back to the studio skin for unknown ids.
	activeSkin = Skins::byId(newSkinId);
	skinId = activeSkin->id();
	darkMode = dark;
	currentTokens = activeSkin->tokens(darkMode);

	QString styleSheet;
	QFile file(activeSkin->qssResource(darkMode));
	if (file.open(QFile::ReadOnly))
	{
		styleSheet = QString::fromUtf8(file.readAll());
	}
	else
	{
		QFile fallback(Skins::byId(QStringLiteral("studio"))->qssResource(darkMode));
		if (fallback.open(QFile::ReadOnly))
			styleSheet = QString::fromUtf8(fallback.readAll());
	}
	qApp->setStyleSheet(substituteTokens(styleSheet, currentTokens));

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();
}

IRoutingRenderer* SkinManager::routingRenderer() const
{
	return activeSkin != nullptr ? activeSkin->routingRenderer() : nullptr;
}

void SkinManager::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state) const
{
	if (activeSkin != nullptr)
		activeSkin->paintKnob(painter, rect, state, currentTokens);
}

QString SkinManager::cardFrameStyle(const CommandRowInfo& info) const
{
	return activeSkin != nullptr ? activeSkin->cardFrameStyle(info, currentTokens) : QString();
}

QString SkinManager::cardHeaderStyle(const CommandRowInfo& info) const
{
	return activeSkin != nullptr ? activeSkin->cardHeaderStyle(info, currentTokens) : QString();
}

QString SkinManager::typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor) const
{
	return activeSkin != nullptr ? activeSkin->typeBadgeStyle(info, typeColor, currentTokens) : QString();
}

void SkinManager::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const
{
	if (activeSkin != nullptr)
		activeSkin->prepareCommandRow(info, card, header, body);
}

void SkinManager::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info) const
{
	if (activeSkin != nullptr)
		activeSkin->paintCardChrome(painter, rect, info, currentTokens);
}
