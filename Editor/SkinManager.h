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
struct KnobState;

class SkinManager : public QObject
{
	Q_OBJECT

public:
	static SkinManager* instance();

	const SkinTokens& tokens() const;
	const QString& currentSkinId() const;
	bool isDark() const;
	void applySkin(const QString& skinId, bool dark);

	// The Copy routing renderer for the active skin. Each skin draws channel
	// routing in a completely different way (crosspoint matrix, step list, node
	// graph, ...). Returns nullptr when the skin has no dedicated renderer yet,
	// in which case the caller falls back to the legacy CopyFilterGUI.
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
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const;
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info) const;

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

	ISkin* activeSkin = nullptr;
	SkinTokens currentTokens;
	QString skinId = QStringLiteral("studio");
	bool darkMode = true;
};
