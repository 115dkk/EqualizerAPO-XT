#include "SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QWidget>

#include "helpers/LogHelper.h"
#include "Editor/helpers/CrashHandler.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/cards/DefaultReferenceCardView.h"
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
	// Breadcrumb + unconditional log line: a skin-switch crash reported from
	// the field (only two of five skins survived on a PC-bang machine, not
	// reproducible on the dev machine) must identify the dying skin in the
	// crash report and in %TEMP%\EqualizerAPO.log.
	CrashHandler::setBreadcrumb(QStringLiteral("applySkin %1 dark=%2").arg(newSkinId).arg(dark).toStdWString());
	LogFStatic(L"Applying skin %s (dark=%d)", reinterpret_cast<const wchar_t*>(newSkinId.utf16()), dark ? 1 : 0);

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
	// Combo-box and spin-box arrows: every skin draws these with the CSS-border
	// triangle trick (image: none on a 0x0 box plus coloured borders). On Qt 6.10
	// that collapses to a flat dash instead of a triangle, so the dropdown and
	// up/down arrows render as a "-". The offscreen skin gallery only ever
	// exercised filter cards, so it never caught this. Override the arrow
	// sub-controls app-wide with a real chevron SVG (reliable across Qt versions
	// and DPI). Appended after the skin sheet so it wins on equal specificity; the
	// chevron is a neutral muted grey that reads on both dark and light skins.
	static const QString arrowOverride = QStringLiteral(
		"QComboBox::down-arrow,"
		"QComboBox[paramSelector=\"true\"]::down-arrow,"
		"QComboBox[filterSelector=\"true\"]::down-arrow {"
		" image: url(:/icons/modern/chevron-down.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }"
		"QAbstractSpinBox::up-arrow {"
		" image: url(:/icons/modern/chevron-up.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }"
		"QAbstractSpinBox::down-arrow {"
		" image: url(:/icons/modern/chevron-down.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }");
	qApp->setStyleSheet(substituteTokens(styleSheet, currentTokens) + arrowOverride);

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();

	LogFStatic(L"Skin %s applied", reinterpret_cast<const wchar_t*>(skinId.utf16()));
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

FilterPickerView* SkinManager::createFilterPicker(QWidget* parent) const
{
	if (activeSkin != nullptr)
		return activeSkin->createFilterPicker(parent);
	return new DefaultFilterPickerView(parent);
}

ReferenceCardView* SkinManager::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	if (activeSkin != nullptr)
		return activeSkin->createReferenceCardView(kind, parent);
	return new DefaultReferenceCardView(parent);
}

void SkinManager::paintTitleBarChrome(QPainter& painter, const QRect& rect) const
{
	if (activeSkin != nullptr)
		activeSkin->paintTitleBarChrome(painter, rect, currentTokens);
}

void SkinManager::styleMainToolbar(QToolBar* toolBar) const
{
	if (toolBar == nullptr || activeSkin == nullptr)
		return;
	// Reset the shared mutable toolbar state before delegating so one skin's
	// choices cannot leak across a live skin switch (minimal sets
	// Qt::ToolButtonTextOnly; everyone else expects icon-only).
	toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	activeSkin->styleMainToolbar(toolBar, currentTokens);
}
