#pragma once

#include <QObject>
#include <QString>

#include "SkinTokens.h"

class SkinManager : public QObject
{
	Q_OBJECT

public:
	static SkinManager* instance();

	const SkinTokens& tokens() const;
	QString currentSkinId() const;
	bool isDark() const;
	void applySkin(const QString& skinId, bool dark);

signals:
	void skinChanged(const SkinTokens& tokens);

private:
	explicit SkinManager(QObject* parent = nullptr);
	SkinTokens loadTokens(const QString& skinId, bool dark) const;
	QString qssPath(const QString& skinId, bool dark) const;

	SkinTokens currentTokens;
	QString skinId = QStringLiteral("studio");
	bool darkMode = true;
};
