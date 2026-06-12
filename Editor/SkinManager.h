#pragma once

#include <QObject>
#include <QString>

#include "SkinTokens.h"

class IRoutingRenderer;
class ISkin;
class QPainter;
class QRect;
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

signals:
	void skinChanged(const SkinTokens& tokens);

private:
	explicit SkinManager(QObject* parent = nullptr);

	ISkin* activeSkin = nullptr;
	SkinTokens currentTokens;
	QString skinId = QStringLiteral("studio");
	bool darkMode = true;
};
