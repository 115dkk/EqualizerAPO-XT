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

void testConvolutionPathHelper();
void testAnalysisWorkerRecovery();
void testUpdateInfoFormatter();
void testVelopackUpdateInfo();
void testVelopackGitHubRelease();
void testVelopackFeeds();
void testFilterCardDescriptors();
void testSubwooferRoutingDescriptors();
void testSubwooferRoutingCrossoverRecipes();
void testFilterCardDepths();
void testFilterCardBuildPlans();
void testConfigImport();
void testLegacyMigrationScanAndPolicy();
void testChannelSelectionModel();
void testDeviceSelectionModel();
void testMultiConvolutionRoutingAdapter();
void testStageSelectionModel();
void testStudioRoutingModel();
void testRoutingFold();
void testAnalysisResponseBinArithmetic();
void testAnalysisResponseEmptyAndLatency();
void testResponseCurveMatchesTheLegacyMagnitudePath();
void testResponseCurveFitsAndLabelsTheValueAxis();
void testResponseCurveHandlesEmptyAndDegenerateRequests();
void testResponseCurveFrequencyAxis();
void testResponseCurveSegmentsBreakWhereTheValueIsMissing();
void testPhaseAndGroupDelayOfAUnityResponse();
void testPhaseAndGroupDelayOfAPureDelay();
void testPhaseAndGroupDelayOfAnAllPass();
void testPhaseBreaksWhereTheResponseIsDead();
void testPhaseAndGroupDelayAxisCaptions();
void testBiQuadWidthRoundTripsExactly();
void testBiQuadWidthModesAndDefaults();
void testConfigFileCodec();
void testConfigFileCodecRetriesAtomicReplaceAfterReaderCloses();
void testConfigFileCodecPreservesExistingFileWhenAtomicReplaceFails();
void testConfigFileCodecRejectsPartialRead();
void testMemoryHelperConstructReleasesStorageWhenConstructorThrows();
void testUpdateSessionPublishesStagedVersionAfterJoin();
void testUpdateSessionLaunchesElevatedCoordinatorWithoutApplyingDirectly();
void testUpdateSessionContainsBackgroundFailure();
void testUpdateCoordinatorAppliesPendingRestartThroughAdapter();
void testUpdateSessionReportsUpToDateAndStartsOnlyOnce();
void testUpdateSessionAppliesDirectlyWhenAlreadyElevated();
void testUpdateSessionContainsApplyFailure();
void testUpdateSessionKeepsPendingUpdateWhenElevationIsCancelled();
void testUpdateCoordinatorReportsNoPendingRestart();
void testUpdateCoordinatorContainsAdapterFailure();
void testLegacyMigrationHookAdoptsStableRootThroughThePort();
void testLegacyMigrationHookRescuesVolatileTreeAndLeavesBreadcrumbs();
void testVelopackInstallRootFollowsTheCurrentLeafRule();
void testElevatedCoordinatorArgumentHasOneSpelling();
void testTheSkinRosterIsTheOneList();
void testSkinTokensCarryExplicitMode();
void testTokenSubstitutionOffersAnAlphaForm();
void testEverySkinSheetResolvesAllThemeTokens();
void testEditableValueTextUsesDisplayedDecimalFormatFirst();
void testBenchmarkBatchPlanUsesOnlyComparableFullBatches();
void testFileReferenceControllerOwnsPathState();
void testReferenceCardDerivesSharedPresentationState();
void testVSTBusModelMigratesAndEdits();
void testFilterListModel();
void testFilterListUndo();
void testFilterCommandCatalogRoster();
void testFilterCommandCatalogIconsExistOnDisk();
void testFilterCommandCatalogTemplateRoster();
void testFilterCommandCatalogDescriptions();
void testFilterPickerModelMatchesTermsAndPreservesCatalogIndices();
void testFilterPickerModelOwnsSelectionNavigation();
void testSharedRawBodyAndRoutingViewPredicates();
void testVstChunkPathCandidates();
void testAutoInstallerChannelMapping();
void testAutoInstallerAssetGrammar();
void testAutoInstallerChecksumParsing();
void testAutoInstallerFlagScan();
