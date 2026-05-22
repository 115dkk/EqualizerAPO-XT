#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "Editor/helpers/ConvolutionPathHelper.h"
#include "UpdateChecker/UpdateInfoFormatter.h"

namespace
{
QString normalized(QString path)
{
	return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

void fail(const QString& message)
{
	fprintf(stderr, "EditorLogicTests failed: %s\n", message.toUtf8().constData());
	exit(1);
}

void expectPath(const QString& actual, const QString& expected)
{
	if (normalized(actual) != normalized(expected))
		fail(QString("expected '%1', got '%2'").arg(expected, actual));
}

void expectTrue(bool value, const QString& message)
{
	if (!value)
		fail(message);
}

void expectFalse(bool value, const QString& message)
{
	if (value)
		fail(message);
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QString configPath = "C:/EqualizerAPO/config/config.txt";

	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "irs/room.wav"),
		"C:/EqualizerAPO/config/irs/room.wav");
	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");

	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/EqualizerAPO/config/irs/room.wav"),
		"irs/room.wav");
	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/EqualizerAPO/shared/room.wav"),
		"C:/EqualizerAPO/shared/room.wav");
	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");

	expectTrue(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("irs/room.wav"),
		"relative path inside config directory was rejected");
	expectFalse(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("../shared/room.wav"),
		"parent-directory relative path was accepted");
	expectFalse(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("C:/Impulse/room.wav"),
		"absolute path was accepted as relative");

	QJsonObject release;
	release["download-url"] = "https://example.invalid";
	QJsonArray versions;
	QJsonObject version;
	version["version"] = "<b>2.0</b>";
	version["date"] = "not-a-date";
	version["info"] = QJsonArray({ "<script>alert(1)</script>", "Fix & verify" });
	versions.append(version);
	release["versions"] = versions;

	QString newestVersion;
	QString html = UpdateInfoFormatter::releaseHtml(QJsonDocument(release), &newestVersion);
	if (newestVersion != "<b>2.0</b>")
		fail("newest version was not preserved");
	if (html.contains("<script>") || html.contains("<b>2.0</b>") || html.contains("Fix & verify"))
		fail("release notes were not HTML-escaped");
	if (!html.contains("&lt;script&gt;alert(1)&lt;/script&gt;") || !html.contains("Fix &amp; verify"))
		fail("escaped release notes are missing");

	printf("EditorLogicTests passed\n");
	return 0;
}
