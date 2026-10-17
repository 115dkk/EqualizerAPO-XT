/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

#include "../TestHarness.h"

inline test::HarnessAccess harness;

void fail(const QString& message);
void expectPath(const QString& actual, const QString& expected);
void expectTrue(bool value, const QString& message);
void expectFalse(bool value, const QString& message);
void expectEqual(const QString& actual, const QString& expected, const QString& message);
void expectEqual(int actual, int expected, const QString& message);
void expectEqual(const QStringList& actual, const QStringList& expected, const QString& message);
void expectNear(double actual, double expected, double tolerance, const QString& message);
void requireTrue(bool value, const QString& message);
void requireEqual(int actual, int expected, const QString& message);
