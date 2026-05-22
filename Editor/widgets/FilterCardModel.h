#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>

struct FilterCardDescriptor
{
	QString command;
	QString type;
	QString badge;
	QString title;
	QString summary;
	QString color;
	QStringList channelBadges;
	int depth = 0;
	bool enabled = true;
	bool routeType = false;
};

class FilterCardModel
{
public:
	static FilterCardDescriptor describeLine(const QString& line, int depth = 0);
	static QVector<int> calculateDepths(const QList<QString>& lines);
	static QString commandForLine(const QString& line, QString* parameters);

private:
	static QStringList parseChannelList(const QString& text);
	static QString compactWhitespace(QString text);
};
