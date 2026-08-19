#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>

#include "Tests/TestHarness.h"
#include "EditorLogicTestSupport.h"

// Generic assertion primitives are shared with the other suites via the
// header-only harness. The QString helpers below convert at the boundary so
// EditorLogicTests can keep its Qt-specific checks (expectPath) alongside.
// The policy argument is left off on purpose: FailurePolicy::Collect is the
// harness default now, so one broken feature block does not hide the findings
// of every block after it. The require* wrappers keep the gating checks
// aborting, and main() always reaches report().
test::Harness harness("EditorLogicTests");

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

		testConvolutionPathHelper();
		testAnalysisWorkerRecovery();
		testUpdateInfoFormatter();
		testVelopackUpdateInfo();
		testVelopackGitHubRelease();
		testVelopackFeeds();
		testFilterCardDescriptors();
	testSubwooferRoutingDescriptors();
	testSubwooferRoutingCrossoverRecipes();
		testFilterCardDepths();
		testRowGuiPolicyRoutesEachLineShape();
		testFilterCardBuildPlans();
		testConfigImport();
		testLegacyMigrationScanAndPolicy();
		testChannelSelectionModel();
		testDeviceSelectionModel();
		testMultiConvolutionRoutingAdapter();
		testStageSelectionModel();
		testStudioRoutingModel();
		testRoutingFold();
		testAnalysisResponseBinArithmetic();
		testAnalysisResponseEmptyAndLatency();
		testResponseCurveMatchesTheLegacyMagnitudePath();
		testResponseCurveFitsAndLabelsTheValueAxis();
		testResponseCurveHandlesEmptyAndDegenerateRequests();
		testResponseCurveFrequencyAxis();
		testResponseCurveSegmentsBreakWhereTheValueIsMissing();
		testPhaseAndGroupDelayOfAUnityResponse();
		testPhaseAndGroupDelayOfAPureDelay();
		testPhaseAndGroupDelayOfAnAllPass();
		testPhaseBreaksWhereTheResponseIsDead();
		testPhaseAndGroupDelayAxisCaptions();
		testBiQuadWidthRoundTripsExactly();
		testBiQuadWidthModesAndDefaults();
		testConfigFileCodec();
		testConfigFileCodecRetriesAtomicReplaceAfterReaderCloses();
		testConfigFileCodecPreservesExistingFileWhenAtomicReplaceFails();
		testConfigFileCodecRejectsPartialRead();
		testMemoryHelperConstructReleasesStorageWhenConstructorThrows();
		testUpdateSessionPublishesStagedVersionAfterJoin();
		testUpdateSessionLaunchesElevatedCoordinatorWithoutApplyingDirectly();
		testUpdateSessionContainsBackgroundFailure();
		testUpdateCoordinatorAppliesPendingRestartThroughAdapter();
		testUpdateSessionReportsUpToDateAndStartsOnlyOnce();
		testUpdateSessionAppliesDirectlyWhenAlreadyElevated();
		testUpdateSessionContainsApplyFailure();
		testUpdateSessionKeepsPendingUpdateWhenElevationIsCancelled();
		testUpdateCoordinatorReportsNoPendingRestart();
		testUpdateCoordinatorContainsAdapterFailure();
		testLegacyMigrationHookAdoptsStableRootThroughThePort();
		testLegacyMigrationHookRescuesVolatileTreeAndLeavesBreadcrumbs();
		testSubwooferRoutingUiStateTracksMutationsAndValidation();
		testSubwooferRoutingUiStateRejectsUnknownTargets();
		testSubwooferRoutingUiStateHeadroomModes();
		testVelopackInstallRootFollowsTheCurrentLeafRule();
		testElevatedCoordinatorArgumentHasOneSpelling();
		testTheSkinRosterIsTheOneList();
		testSkinTokensCarryExplicitMode();
		testTokenSubstitutionOffersAnAlphaForm();
		testEverySkinSheetResolvesAllThemeTokens();
		testEditableValueTextUsesDisplayedDecimalFormatFirst();
		testBenchmarkBatchPlanUsesOnlyComparableFullBatches();
		testFileReferenceControllerOwnsPathState();
		testReferenceCardDerivesSharedPresentationState();
		testVSTBusModelMigratesAndEdits();
		testFilterListModel();
		testFilterListUndo();
		testFilterCommandCatalogRoster();
		testFilterCommandCatalogIconsExistOnDisk();
		testFilterCommandCatalogTemplateRoster();
		testFilterCommandCatalogDescriptions();
		testFilterPickerModelMatchesTermsAndPreservesCatalogIndices();
		testFilterPickerModelOwnsSelectionNavigation();
		testSharedRawBodyAndRoutingViewPredicates();
		testVstChunkPathCandidates();
		testAutoInstallerChannelMapping();
		testAutoInstallerAssetGrammar();
		testAutoInstallerChecksumParsing();
		testAutoInstallerFlagScan();

		harness.report();
		return EXIT_SUCCESS;
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
