#pragma once

#include <QCoreApplication>
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
	bool canToggleEnabled = true;
	bool routeType = false;
};

class FilterCardModel
{
	// describeLine() produces the human-readable card title/summary strings. It is
	// a plain (non-QObject) helper, so it cannot inherit QObject::tr(); this macro
	// gives it a static tr() bound to the "FilterCardModel" translation context.
	Q_DECLARE_TR_FUNCTIONS(FilterCardModel)

public:
	static FilterCardDescriptor describeLine(const QString& line, int depth = 0);
	static QVector<int> calculateDepths(const QList<QString>& lines);
	static QString commandForLine(const QString& line, QString* parameters);
	// A line that is a note, not a disabled command; such a line has no
	// "command: parameters" shape, so FilterTable routes it to the comment card.
	static bool isPureCommentLine(const QString& line);

private:
	static QStringList parseChannelList(const QString& text);
	static QString compactWhitespace(const QString& text);
	static bool isDisabledCommandLine(const QString& line);
};
