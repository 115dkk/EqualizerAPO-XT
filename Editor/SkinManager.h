#pragma once

#include <QObject>
#include <QString>

#include "SkinTokens.h"

class IRoutingRenderer;
class ISkin;

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

signals:
	void skinChanged(const SkinTokens& tokens);

private:
	explicit SkinManager(QObject* parent = nullptr);

	ISkin* activeSkin = nullptr;
	SkinTokens currentTokens;
	QString skinId = QStringLiteral("studio");
	bool darkMode = true;
};
