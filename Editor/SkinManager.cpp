#include "SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QWidget>

#include "services/logging/Logging.h"
#include "Editor/helpers/CrashHandler.h"
#include "skins/ISkin.h"
#include "skins/Skins.h"
#include "Editor/skins/HeritageSkin.h"
#include "skins/SkinThemeData.h"

SkinManager::SkinManager(QObject* parent)
	: QObject(parent)
{
	// Establishes the never-null invariant on activeSkin (see the header).
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
	// The heritage identity - classic light tokens, no QSS, no routing view,
	// native toolbar/dialog, neutral base painters - lives in HeritageSkin
	// (audit #275 B5); this method only does the app-level work.
	activeSkin = heritageSkin();
	skinId = activeSkin->id();
	darkMode = false;
	currentTokens = activeSkin->tokens(false);

	qApp->setStyleSheet(QString());
	qApp->setPalette(qApp->style()->standardPalette());

	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();

	LogFStatic(L"Heritage presentation applied");
}

// The @TOKEN@ substitution lives in SkinThemeData::substituteTokens so
// satellite tools (DeviceSelector) dress the same sheets identically.

void SkinManager::applySkin(const QString& newSkinId, bool dark)
{
	// Re-dressing the app with the identical sheet is not free: Qt re-resolves
	// the stylesheet against every live widget. At startup this used to run
	// three times (main(), loadPreferences() before and after the open-files
	// restore); the post-restore pass alone re-polished every filter card and
	// took seconds on a large config.
	if (sheetApplied && !heritageMode && darkMode == dark
		&& Skins::byId(newSkinId)->id() == skinId)
	{
		LogFStatic(L"Skin %s (dark=%d) already active, skipping re-apply",
			reinterpret_cast<const wchar_t*>(skinId.utf16()), dark ? 1 : 0);
		return;
	}

	heritageMode = false;
	// Breadcrumb + unconditional log line: a skin-switch crash reported from
	// the field must identify the dying skin in the crash report and in
	// %TEMP%\EqualizerAPO.log.
	CrashHandler::setBreadcrumb(QStringLiteral("applySkin %1 dark=%2").arg(newSkinId).arg(dark).toStdWString());
	LogFStatic(L"Applying skin %s (dark=%d)", reinterpret_cast<const wchar_t*>(newSkinId.utf16()), dark ? 1 : 0);

	// Skins::byId applies legacy aliases (glassy->studio, industrial->rack) and
	// falls back to the studio skin for unknown ids.
	activeSkin = Skins::byId(newSkinId);
	skinId = activeSkin->id();
	darkMode = dark;
	currentTokens = activeSkin->tokens(darkMode);

	// The process-wide QSS/palette/font contract is shared with companion
	// executables. The Editor keeps its CustomStyle, so Fusion is not reset.
	SkinThemeData::applyToApplication(*qApp, skinId, darkMode, false, true);

	sheetApplied = true;
	emit skinChanged(currentTokens);
	for (QWidget* widget : qApp->allWidgets())
		widget->update();

	LogFStatic(L"Skin %s applied", reinterpret_cast<const wchar_t*>(skinId.utf16()));
}

// The forwarders below delegate without a null check on purpose: activeSkin
// is never null (class invariant, see the header). Only genuinely different
// behavior - the heritage branches - earns a conditional.

IRoutingRenderer* SkinManager::routingRenderer() const
{
	return activeSkin->routingRenderer();
}

void SkinManager::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state) const
{
	// HeritageSkin inherits the base painter, which is exactly the heritage
	// AudioKnob rendering; no conditional needed.
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

BadgeTreatment SkinManager::badgeTreatment(const CommandRowInfo& info, const QString& typeColor, const QString& badgeToken) const
{
	return activeSkin->badgeTreatment(info, typeColor, badgeToken, currentTokens);
}

void SkinManager::prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const
{
	// The tokens come from here rather than from each skin reaching back into this
	// singleton, which is what all five did before the hook took them.
	activeSkin->prepareCommandRow(info, card, header, body, tokens());
}

void SkinManager::paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info) const
{
	activeSkin->paintCardChrome(painter, rect, info, currentTokens);
}

bool SkinManager::paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info) const
{
	return activeSkin->paintScopeGutter(painter, size, info, currentTokens);
}

bool SkinManager::logicSiblingsIndentAsMembers() const
{
	return activeSkin->logicSiblingsIndentAsMembers();
}

void SkinManager::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state) const
{
	activeSkin->paintAddRow(painter, rect, state, currentTokens);
}

void SkinManager::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state) const
{
	activeSkin->paintInsertSeam(painter, rect, state, currentTokens);
}

void SkinManager::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state) const
{
	activeSkin->paintGraphicEqPlot(painter, state, currentTokens);
}

void SkinManager::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state) const
{
	activeSkin->paintAnalysisGraph(painter, state, currentTokens);
}

void SkinManager::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state) const
{
	activeSkin->paintSegmentedControl(painter, state, currentTokens);
}

void SkinManager::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state) const
{
	activeSkin->paintVstBusSelector(painter, state, currentTokens);
}

void SkinManager::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state) const
{
	activeSkin->paintVstBusFrame(painter, state, currentTokens);
}

FilterPickerView* SkinManager::createFilterPicker(QWidget* parent) const
{
	return activeSkin->createFilterPicker(parent, tokens());
}

ReferenceCardView* SkinManager::createReferenceCardView(const QString& kind, QWidget* parent) const
{
	return activeSkin->createReferenceCardView(kind, parent, tokens());
}

SubwooferRoutingCardView* SkinManager::createSubwooferRoutingCardView(QWidget* parent) const
{
	return activeSkin->createSubwooferRoutingCardView(parent, tokens());
}

void SkinManager::paintTitleBarChrome(QPainter& painter, const QRect& rect) const
{
	activeSkin->paintTitleBarChrome(painter, rect, currentTokens);
}

void SkinManager::styleMainToolbar(QToolBar* toolBar) const
{
	if (toolBar == nullptr)
		return;
	// Reset the shared mutable toolbar state before delegating so one skin's
	// choices cannot leak across a live skin switch (minimal sets
	// Qt::ToolButtonTextOnly; everyone else expects icon-only).
	toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	activeSkin->styleMainToolbar(toolBar, currentTokens);
}

void SkinManager::styleFileDialog(QFileDialog* dialog) const
{
	activeSkin->styleFileDialog(dialog, currentTokens);
}
