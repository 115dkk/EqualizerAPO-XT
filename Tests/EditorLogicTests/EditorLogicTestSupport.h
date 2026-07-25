#pragma once

#include <QString>
#include <QStringList>

#include "Tests/TestHarness.h"

extern test::Harness harness;

void fail(const QString& message);
void expectPath(const QString& actual, const QString& expected);
void expectTrue(bool value, const QString& message);
void expectFalse(bool value, const QString& message);
void expectEqual(const QString& actual, const QString& expected, const QString& message);
void expectEqual(int actual, int expected, const QString& message);
void expectEqual(const QStringList& actual, const QStringList& expected, const QString& message);
void requireTrue(bool value, const QString& message);
void requireEqual(int actual, int expected, const QString& message);

void testAnalysisResponseBinArithmetic();
void testAnalysisResponseEmptyAndLatency();
void testConfigFileCodec();
void testConfigFileCodecPreservesExistingFileWhenAtomicReplaceFails();
void testConfigFileCodecRejectsPartialRead();
void testOwnedBackgroundTaskJoinsAndStartsOnlyOnce();
void testSkinTokensCarryExplicitMode();
void testEverySkinSheetResolvesAllThemeTokens();
void testEditableValueTextUsesDisplayedDecimalFormatFirst();
void testBenchmarkBatchPlanUsesOnlyComparableFullBatches();
void testFileReferenceControllerOwnsPathState();
