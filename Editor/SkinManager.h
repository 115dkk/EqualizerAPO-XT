#pragma once

#include <QObject>
#include <QString>

#include "SkinTokens.h"

class FilterPickerView;
class IRoutingRenderer;
class ISkin;
class QPainter;
class QRect;
class QToolBar;
class QWidget;
class ReferenceCardView;
struct CommandRowInfo;
struct GraphicEQPlotState;
struct KnobState;
struct ListChromeState;

class SkinManager : public QObject
{
	Q_OBJECT

public:
	static SkinManager* instance();

	const SkinTokens& tokens() const;
	const QString& currentSkinId() const;
	bool isDark() const;
	void applySkin(const QString& skinId, bool dark);

	// The heritage presentation behind the LegacyRows mode: no stylesheet, the
	// platform's standard palette, and classic light token values for the few
	// custom painters (analysis graph, knobs). The legacy rows are meant to be
	// the unmodernized original editor, not skinned widgets inside modern
	// chrome. (maintainer decision 2026-07-05)
	void applyHeritage();
	bool isHeritage() const;

	// The Copy routing renderer for the active skin. Each skin draws channel
	// routing in a completely different way (crosspoint matrix, step list, node
	// graph, ...). Returns nullptr when the skin has no dedicated renderer yet,
	// in which case the caller falls back to the legacy CopyFilterGUI.
	// Always nullptr in heritage mode so Copy falls back to the classic
	// CopyFilterGUI graphics scene.
	IRoutingRenderer* routingRenderer() const;

	// Paint a knob through the active skin (ISkin::paintKnob) with the current
	// tokens. Called by AudioKnob::paintEvent; the widget keeps all input
	// handling and hands only the painting to the skin.
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state) const;

	// Per-command-type row chrome, delegated to the active skin with the
	// current tokens (see the ISkin hooks for semantics).
	QString cardFrameStyle(const CommandRowInfo& info) const;
	QString cardHeaderStyle(const CommandRowInfo& info) const;
	QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor) const;
	QColor typeBadgeInk(const CommandRowInfo& info, const QString& typeColor, const QString& badgeToken) const;
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const;
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info) const;

	// List-level insertion chrome, delegated to the active skin (see
	// ISkin::paintAddRow / ISkin::paintInsertSeam). In heritage mode the
	// AddCardRow/FilterInsertSeam widgets are never created, so these are only
	// reached with an active skin.
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state) const;
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state) const;

	// The GraphicEQ card's response plot (ISkin::paintGraphicEqPlot).
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state) const;

	// The "add filter" picker view for the active skin (ISkin::createFilterPicker).
	FilterPickerView* createFilterPicker(QWidget* parent) const;

	// The reference-card body view for the active skin
	// (ISkin::createReferenceCardView). kind is ReferenceCardState::kind.
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const;

	// Main toolbar icons/chrome for the active skin (ISkin::styleMainToolbar).
	void styleMainToolbar(QToolBar* toolBar) const;

	// Painted title-bar decoration for the active skin (ISkin::paintTitleBarChrome).
	void paintTitleBarChrome(QPainter& painter, const QRect& rect) const;

signals:
	void skinChanged(const SkinTokens& tokens);

private:
	explicit SkinManager(QObject* parent = nullptr);

	// Class invariant: never null. Seeded in the constructor and only ever
	// reassigned through Skins::byId (which falls back to studio), so the
	// hook forwarders in the .cpp delegate without a null check.
	ISkin* activeSkin = nullptr;
	SkinTokens currentTokens;
	QString skinId = QStringLiteral("studio");
	bool darkMode = true;
	bool heritageMode = false;
};
