/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

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
void testFilterCardDescriptors();
void testCardEditsKeepSwitchedOffLinesOff();
void testSubwooferRoutingDescriptors();
void testSubwooferRoutingCrossoverRecipes();
void testFilterCardDepths();
void testRowGuiPolicyRoutesEachLineShape();
void testFilterCardBuildPlans();
void testConfigImport();
void testFileReferencesReadLikeTheEngine();
void testLegacyMigrationScanAndPolicy();
void testChannelSelectionModel();
void testDeviceSelectionModel();
void testMultiConvolutionRoutingAdapter();
void testStageSelectionModel();
void testRoutingGridModelPorts();
void testRoutingGridModel();
void testRoutingFold();
void testSourceTokenGrammar();
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
void testKnobTravelLaw();
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
void testLegacyMigrationHookLeavesAnUnreadableConfigPathAlone();
void testCallerProfileCheckRejectsUnsafePaths();
void testLegacyMigrationHookUsesVerifiedCallerOrFallsBack();
void testConfigHandleGrantRejectsJunctionsAndPreservesChildren();
void testCommandLineQuotingRoundTripsThroughCommandLineToArgvW();
void testVelopackInstallRootFollowsTheCurrentLeafRule();
void testSubwooferRoutingUiStateTracksMutationsAndValidation();
void testSubwooferRoutingDefaultStates();
void testFilterLineCardDecidesPerLine();
void testSubwooferRoutingUiStateRejectsUnknownTargets();
void testSubwooferRoutingUiStateHeadroomModes();
void testSubwooferRoutingReadsRoundTripEverySetter();
void testSubwooferRoutingSourceLfeGainReaders();
void testSubwooferRoutingPreviewRateAndTrim();
void testSubwooferRoutingPresetDisplayName();
void testAnalysisRequestFence();
void testImpulseMeasurementFindsTheDelay();
void testImpulseMeasurementAtTheBlockStart();
void testImpulseMeasurementWithoutAnImpulse();
void testImpulsePeakGain();
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
void testVSTSlotFillModel();
void testChannelFlow();
void testVSTRowDocument();
void testPanelMonitorGateOpensOnlyForSelfGeneratedAudio();
void testPanelMonitorGateArmDelayOutlivesReverbTails();
void testPanelMonitorGateClosesAndRearms();
void testPanelMonitorGateInterruptedArmStartsOver();
void testFilterListModel();
void testFilterListUndo();
void testFilterCommandCatalogRoster();
void testFilterCommandCatalogIconsExistOnDisk();
void testFilterCommandCatalogTemplateRoster();
void testFilterCommandCatalogDescriptions();
void testFilterCommandCatalogChannelSelectionTargets();
void testFilterPickerModelMatchesTermsAndPreservesCatalogIndices();
void testFilterPickerModelOwnsSelectionNavigation();
void testSharedRawBodyAndRoutingViewPredicates();
void testVstChunkPathCandidates();
void testAutoInstallerChannelMapping();
void testAutoInstallerChannelDescriptions();
void testAutoInstallerAssetGrammar();
void testAutoInstallerChecksumParsing();
void testAutoInstallerFlagScan();
void testInstallerUiModelStepTransitions();
void testInstallerUiModelFormatting();
