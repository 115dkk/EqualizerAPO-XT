#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include "Editor/helpers/ConvolutionPathHelper.h"

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

	printf("EditorLogicTests passed\n");
	return 0;
}
