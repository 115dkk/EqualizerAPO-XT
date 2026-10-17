/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>

#include "Tests/AlignedMemoryGate.h"
#include "../TestHarness.h"
#include "EditorLogicTestSupport.h"

// Generic assertion primitives are shared with the other suites via the
// header-only harness. The QString helpers below convert at the boundary so
// EditorLogicTests can keep its Qt-specific checks (expectPath) alongside.
// runAll() owns the suite harness. The require* wrappers keep gating checks
// aborting while ordinary expectations collect failures through the full suite.
std::string toStd(const QString& s)
{
	return s.toUtf8().constData();
}

QString normalized(const QString& path)
{
	return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

void fail(const QString& message)
{
	harness.fail(toStd(message));
}

void expectPath(const QString& actual, const QString& expected)
{
	harness.expectTrue(
		normalized(actual) == normalized(expected),
		toStd(QString("expected '%1', got '%2'").arg(expected, actual)));
}

void expectTrue(bool value, const QString& message)
{
	harness.expectTrue(value, toStd(message));
}

void expectFalse(bool value, const QString& message)
{
	harness.expectFalse(value, toStd(message));
}

void expectEqual(const QString& actual, const QString& expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected '%2', got '%3'").arg(message, expected, actual)));
}

void expectEqual(int actual, int expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected %2, got %3").arg(message).arg(expected).arg(actual)));
}

void expectEqual(const QStringList& actual, const QStringList& expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected '%2', got '%3'").arg(message, expected.join(' '), actual.join(' '))));
}

void expectNear(double actual, double expected, double tolerance, const QString& message)
{
	harness.expectNear(actual, expected, tolerance, toStd(message));
}

// require counterparts of the wrappers above: same message format, but they
// abort on failure regardless of the Collect policy. Use them for checks
// whose failure would make the following lines unsafe (size checks before
// indexing, fixture-creation checks).
void requireTrue(bool value, const QString& message)
{
	harness.require(value, toStd(message));
}

void requireEqual(int actual, int expected, const QString& message)
{
	harness.require(
		actual == expected,
		toStd(QString("%1: expected %2, got %3").arg(message).arg(expected).arg(actual)));
}

int main(int argc, char** argv)
{
	try
	{
		QCoreApplication app(argc, argv);
		return test::runAll("EditorLogicTests");
	}
	catch (const std::exception& error)
	{
		std::cerr << "Unhandled exception escaped a test: " << error.what() << '\n';
	}
	catch (...)
	{
		std::cerr << "A non-standard exception escaped a test.\n";
	}

	return EXIT_FAILURE;
}
