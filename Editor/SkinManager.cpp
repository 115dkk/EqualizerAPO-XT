#include "SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QWidget>

#include "helpers/LogHelper.h"
#include "Editor/helpers/CrashHandler.h"
#include "skins/ISkin.h"
#include "skins/Skins.h"
#include "skins/SkinThemeData.h"

SkinManager::SkinManager(QObject* parent)
	: QObject(parent)
{
	// Establishes the class invariant every forwarder below relies on:
	// activeSkin is NEVER null. Skins::byId falls back to studio for unknown
	// ids, and applySkin/applyHeritage only ever reassign through it, so the
	// hook forwarders delegate without a null check.
	activeSkin = Skins::byId(skinId);
	Q_ASSERT(activeSkin != nullptr);
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

bool SkinManager::isHeritage() const
{
	return heritageMode;
}

void SkinManager::applyHeritage()
{
	CrashHandler::setBreadcrumb(L"applyHeritage (legacy rows)");
	LogFStatic(L"Applying heritage presentation (legacy rows)");

	heritageMode = true;
	// Token donor and the base-class knob painter; nothing of the skin's own
	// look survives below.
	activeSkin = Skins::byId(QStringLiteral("studio"));
	skinId = QStringLiteral("heritage");
	darkMode = false;

	// Classic light values for the custom painters that consume tokens. The
	// widget chrome itself comes from the native style, untouched by QSS.
	SkinTokens tokens = activeSkin->tokens(false);
	tokens.background = QStringLiteral("#f0f0f0");
	tokens.surface = QStringLiteral("#ffffff");
	tokens.surfaceRaised = QStringLiteral("#f5f5f5");
	tokens.surfaceSunken = QStringLiteral("#e8e8e8");
	tokens.card = QStringLiteral("#ffffff");
	tokens.cardHover = QStringLiteral("#f0f6fc");
	tokens.text = QStringLiteral("#000000");
	tokens.mutedText = QStringLiteral("#606060");
	tokens.border = QStringLiteral("#adadad");
	tokens.graph = QStringLiteral("#ffffff");
	tokens.graphGridMajor = QStringLiteral("#c8c8c8");
	tokens.graphGridMinor = QStringLiteral("#e4e4e4");
	tokens.accent = QStringLiteral("#0078d7");
	tokens.accent2 = QStringLiteral("#2b88d8");
	tokens.focusRing = QStringLiteral("#0078d7");
	tokens.fontFamily = QStringLiteral("Segoe UI");
	tokens.monoFontFamily = QStringLiteral("Consolas");
	currentTokens = tokens;

	qApp->setStyleSheet(QString());
	qApp->setPalette(qApp->style()->standardPalette());

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();

	LogFStatic(L"Heritage presentation applied");
}

// The @TOKEN@ substitution moved to SkinThemeData::substituteTokens so
// satellite tools (DeviceSelector) dress the same sheets identically.

void SkinManager::applySkin(const QString& newSkinId, bool dark)
{
	heritageMode = false;
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
	// up/down arrows render as a "-". The override (SkinThemeData) replaces the
	// arrow sub-controls app-wide with a real chevron SVG, appended after the
	// skin sheet so it wins on equal specificity.
	qApp->setStyleSheet(SkinThemeData::substituteTokens(styleSheet, currentTokens) + SkinThemeData::comboArrowOverride());

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();

	LogFStatic(L"Skin %s applied", reinterpret_cast<const wchar_t*>(skinId.utf16()));
}

// The forwarders below delegate without a null check on purpose: activeSkin
// is a class invariant (never null, see the constructor). Only genuinely
// different behavior - the heritage branches - earns a conditional.

IRoutingRenderer* SkinManager::routingRenderer() const
{
	if (heritageMode)
		return nullptr;
	return activeSkin->routingRenderer();
}

void SkinManager::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state) const
{
	if (heritageMode)
	{
		// The ISkin base implementation is the pre-skin AudioKnob painter,
		// moved verbatim when the skins were introduced - exactly the
		// heritage knob.
		activeSkin->ISkin::paintKnob(painter, rect, state, currentTokens);
		return;
	}
	activeSkin->paintKnob(painter, rect, state, currentTokens);
}

QString SkinManager::cardFrameStyle(const CommandRowInfo& info) const
{
	return activeSkin->cardFrameStyle(info, currentTokens);
}

QString SkinManager::cardHeaderStyle(const CommandRowInfo& info) const
{
	return activeSkin->cardHeaderStyle(info, currentTokens);
}

QString SkinManager::typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor) const
{
	return activeSkin->typeBadgeStyle(info, typeColor, currentTokens);
}

QColor SkinManager::typeBadgeInk(const CommandRowInfo& info, const QString& typeColor, const QString& badgeToken) const
{
	return activeSkin->typeBadgeInk(info, typeColor, badgeToken, currentTokens);
}

void SkinManager::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const
{
	activeSkin->prepareCommandRow(info, card, header, body);
}

void SkinManager::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info) const
{
	activeSkin->paintCardChrome(painter, rect, info, currentTokens);
}

void SkinManager::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state) const
{
	activeSkin->paintAddRow(painter, rect, state, currentTokens);
}

void SkinManager::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state) const
{
	activeSkin->paintInsertSeam(painter, rect, state, currentTokens);
}

FilterPickerView* SkinManager::createFilterPicker(QWidget* parent) const
{
	return activeSkin->createFilterPicker(parent);
}

ReferenceCardView* SkinManager::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return activeSkin->createReferenceCardView(kind, parent);
}

void SkinManager::paintTitleBarChrome(QPainter& painter, const QRect& rect) const
{
	activeSkin->paintTitleBarChrome(painter, rect, currentTokens);
}

void SkinManager::styleMainToolbar(QToolBar* toolBar) const
{
	if (heritageMode)
		return; // native toolbar: the .ui's classic icons stay in place
	if (toolBar == nullptr)
		return;
	// Reset the shared mutable toolbar state before delegating so one skin's
	// choices cannot leak across a live skin switch (minimal sets
	// Qt::ToolButtonTextOnly; everyone else expects icon-only).
	toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	activeSkin->styleMainToolbar(toolBar, currentTokens);
}
