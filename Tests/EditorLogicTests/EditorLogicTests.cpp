#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportExecutor.h"
#include "Editor/import/ImportManifest.h"
#include "Editor/widgets/FilterCardModel.h"
#include "Editor/widgets/cards/ChannelSelectionModel.h"
#include "Editor/widgets/cards/DeviceSelectionModel.h"
#include "Editor/widgets/routing/MultiConvolutionRoutingAdapter.h"
#include "UpdateChecker/UpdateInfoFormatter.h"
#include "UpdateChecker/VelopackUpdateInfo.h"

#include "Tests/TestHarness.h"

namespace
{
// Generic assertion primitives are shared with the other suites via the
// header-only harness. The QString helpers below convert at the boundary so
// EditorLogicTests can keep its Qt-specific checks (expectPath) alongside.
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
		ConvolutionPathHelper::relativePathLooksContainedLexically("irs/room.wav"),
		"relative path inside config directory was rejected");
	expectFalse(
		ConvolutionPathHelper::relativePathLooksContainedLexically("../shared/room.wav"),
		"parent-directory relative path was accepted");
	expectFalse(
		ConvolutionPathHelper::relativePathLooksContainedLexically("C:/Impulse/room.wav"),
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

	expectEqual(
		VelopackUpdateInfo::githubLatestReleaseUrl("https://github.com/115dkk/EqualizerAPO-XT/"),
		"https://api.github.com/repos/115dkk/EqualizerAPO-XT/releases/latest",
		"GitHub latest release URL");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx2"),
		"releases.x64-avx2.json",
		"Velopack feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-sse2"),
		"releases.x64-sse2.json",
		"Velopack SSE2 feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx"),
		"releases.x64-avx.json",
		"Velopack AVX feed file name");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("v1.4.4-main.77", "1.4.3"),
		"Velopack package version was not considered newer");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("1.4.3-main.77", "1.4.3"),
		"CI package version was not considered newer than the base installed version");
	expectFalse(
		VelopackUpdateInfo::isNewerVersion("1.4.0", "1.4.3"),
		"older package version was considered newer");

	QJsonObject githubRelease;
	githubRelease["tag_name"] = "v1.4.4-main.77";
	githubRelease["name"] = "EqualizerAPO-XT 1.4.4-main.77";
	githubRelease["html_url"] = "https://github.com/115dkk/EqualizerAPO-XT/releases/tag/v1.4.4-main.77";
	githubRelease["published_at"] = "2026-05-22T00:00:00Z";
	githubRelease["body"] = "Fix convolution updates\nShip Velopack feeds";
	QJsonArray releaseAssets;
	releaseAssets.append(QJsonObject({
		{ "name", "releases.x64-avx2.json" },
		{ "browser_download_url", "https://example.invalid/releases.x64-avx2.json" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
		{ "browser_download_url", "https://example.invalid/full.nupkg" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-Setup.exe" },
		{ "browser_download_url", "https://example.invalid/setup.exe" },
	}));
	githubRelease["assets"] = releaseAssets;
	QJsonDocument githubReleaseDoc(githubRelease);

	expectTrue(
		VelopackUpdateInfo::isGitHubRelease(githubReleaseDoc),
		"GitHub release response was not detected");
	expectEqual(
		VelopackUpdateInfo::feedAssetUrl(githubReleaseDoc, "x64-avx2"),
		"https://example.invalid/releases.x64-avx2.json",
		"Velopack feed asset URL");
	expectEqual(
		VelopackUpdateInfo::fromGitHubRelease(githubReleaseDoc, "x64-avx2", "1.4.3").object().value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"GitHub release fallback setup URL");

	QJsonArray feedAssets;
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx2" },
		{ "Version", "1.4.4-main.77" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
	}));
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx512" },
		{ "Version", "1.4.5-main.1" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx512-1.4.5-main.1-full.nupkg" },
	}));
	QJsonDocument feedDoc(QJsonObject({ { "Assets", feedAssets } }));
	QJsonDocument updateDoc = VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.3");
	QJsonObject updateObj = updateDoc.object();
	expectEqual(
		updateObj.value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"Velopack setup URL");
	QJsonObject velopackVersion = updateObj.value("versions").toArray().first().toObject();
	expectEqual(
		velopackVersion.value("version").toString(),
		"1.4.4-main.77",
		"Velopack update version");
	QStringList infoLines;
	for (const QJsonValue& infoValue : velopackVersion.value("info").toArray())
		infoLines.append(infoValue.toString());
	expectTrue(
		infoLines.contains("Velopack channel: x64-avx2") && infoLines.contains("Fix convolution updates"),
		"Velopack release notes were not preserved");
	expectTrue(
		VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.4-main.77").isEmpty(),
		"same Velopack version produced an update");

	FilterCardDescriptor preamp = FilterCardModel::describeLine("Preamp: -6 dB");
	expectEqual(preamp.badge, "PRE", "preamp card badge");
	expectEqual(preamp.title, "Preamp", "preamp card title");
	expectEqual(preamp.summary, "-6 dB", "preamp card summary");
	expectTrue(preamp.enabled, "preamp card was marked disabled");

	FilterCardDescriptor disabledFilter = FilterCardModel::describeLine("# Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectFalse(disabledFilter.enabled, "commented filter was marked enabled");
	expectEqual(disabledFilter.badge, "PK", "disabled biquad badge");
	expectEqual(disabledFilter.title, "Peaking", "disabled biquad title");
	expectEqual(disabledFilter.summary, "Fc 1000 Hz · Gain -3 dB · Q 0.71", "disabled biquad summary");

	FilterCardDescriptor lowShelfCenterFilter = FilterCardModel::describeLine("Filter: ON LSC 12 dB Fc 200 Hz Gain 3 dB");
	expectEqual(lowShelfCenterFilter.badge, "LSC", "low-shelf center badge");
	expectEqual(lowShelfCenterFilter.title, "Low-shelf", "low-shelf center title");
	expectTrue(lowShelfCenterFilter.summary.contains("Center 200 Hz") && lowShelfCenterFilter.summary.contains("Gain 3 dB") && lowShelfCenterFilter.summary.contains("Slope 12 dB/Oct"),
		QStringLiteral("low-shelf center summary missing expected fields: ") + lowShelfCenterFilter.summary);

	FilterCardDescriptor lowShelfCornerFilter = FilterCardModel::describeLine("Filter: ON LS 6 dB Fc 120 Hz Gain -2 dB");
	expectEqual(lowShelfCornerFilter.badge, "LS", "low-shelf corner badge");
	expectEqual(lowShelfCornerFilter.title, "Low-shelf", "low-shelf corner title");
	expectTrue(lowShelfCornerFilter.summary.contains("Corner 120 Hz") && lowShelfCornerFilter.summary.contains("Gain -2 dB") && lowShelfCornerFilter.summary.contains("Slope 6 dB/Oct"),
		QStringLiteral("low-shelf corner summary missing expected fields: ") + lowShelfCornerFilter.summary);

	FilterCardDescriptor pureComment = FilterCardModel::describeLine("    # purely explanatory comment");
	expectFalse(pureComment.enabled, "pure comment was marked enabled");
	expectFalse(pureComment.canToggleEnabled, "pure comment should not expose enable toggling");
	expectEqual(pureComment.badge, "#", "pure comment badge");
	expectEqual(pureComment.title, "Comment", "pure comment title");

	FilterCardDescriptor colonComment = FilterCardModel::describeLine("# TODO: explain headphone preset");
	expectEqual(colonComment.title, "Comment", "colon comment title");
	expectFalse(colonComment.canToggleEnabled, "colon comments should not become disabled commands");

	FilterCardDescriptor graphicEq = FilterCardModel::describeLine("GraphicEQ: 20 -1; 100 0; 1000 2");
	expectEqual(graphicEq.badge, "GEQ", "graphic eq badge");
	expectEqual(graphicEq.summary, "3 bands", "graphic eq band count");

	FilterCardDescriptor copy = FilterCardModel::describeLine("Copy: VL=L VR=R L=VL R=VR");
	expectEqual(copy.badge, "CPY", "copy card badge");
	expectEqual(copy.summary, "4 steps, 2 virtual", "copy card summary");
	expectTrue(copy.channelBadges.contains("L") && copy.channelBadges.contains("R"), "copy card did not expose final physical channels");

	FilterCardDescriptor channel = FilterCardModel::describeLine("Channel: L, R");
	expectEqual(channel.badge, "CH", "channel card badge");
	expectEqual(channel.summary, "L R", "channel card summary");
	expectTrue(channel.channelBadges.contains("L") && channel.channelBadges.contains("R"), "channel badges were not parsed");

	// MultiConvolution must get its own card header rather than falling through to
	// the generic TXT descriptor. Its grammar is "<output channel> <IR path>", so
	// the summary leads with the channel and ends with the file name.
	FilterCardDescriptor multiConv = FilterCardModel::describeLine("MultiConvolution: L brir.wav");
	expectEqual(multiConv.badge, "MCONV", "multiconvolution card badge");
	expectEqual(multiConv.title, "MultiConvolution", "multiconvolution card title");
	expectEqual(multiConv.type, "convolution", "multiconvolution shares the convolution row type");
	expectTrue(multiConv.summary.startsWith("L") && multiConv.summary.contains("brir.wav"),
		QStringLiteral("multiconvolution summary should show channel and file: ") + multiConv.summary);

	// A freshly inserted bare "MultiConvolution:" template (no channel/path yet)
	// still classifies as multiconvolution so the header keeps its badge instead of
	// rendering as a generic text row.
	FilterCardDescriptor multiConvBare = FilterCardModel::describeLine("MultiConvolution:");
	expectEqual(multiConvBare.badge, "MCONV", "bare multiconvolution keeps its badge");
	expectEqual(multiConvBare.type, "convolution", "bare multiconvolution keeps convolution styling");

	QVector<int> depths = FilterCardModel::calculateDepths(QList<QString>({
		"Channel: L R",
		"Preamp: -6 dB",
		"Include: nested.txt",
		"Delay: 10 ms",
		"# Channel: R",
		"Preamp: -4 dB",
		"Channel: ALL",
		"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71"
	}));
	expectEqual(depths.size(), 8, "channel depth count");
	expectEqual(depths[0], 0, "channel command depth");
	expectEqual(depths[1], 1, "scoped preamp depth");
	expectEqual(depths[2], 1, "include depth");
	expectEqual(depths[3], 1, "include should preserve channel depth");
	expectEqual(depths[4], 1, "commented channel must not reset depth");
	expectEqual(depths[5], 1, "post-comment channel depth");
	expectEqual(depths[6], 0, "channel all depth");
	expectEqual(depths[7], 0, "post channel-all depth");

	{
		QTemporaryDir tempDir;
		expectTrue(tempDir.isValid(), "QTemporaryDir must be valid");

		QString surroundDir = tempDir.path() + "/Surround";
		expectTrue(QDir().mkpath(surroundDir), "failed to create Surround dir");

		auto writeText = [](const QString& path, const QString& body) {
			QFile f(path);
			expectTrue(f.open(QIODevice::WriteOnly | QIODevice::Text), QString("could not open %1 for write").arg(path));
			QTextStream ts(&f);
			ts << body;
		};
		auto writeBlob = [](const QString& path, int bytes) {
			QFile f(path);
			expectTrue(f.open(QIODevice::WriteOnly), QString("could not open %1 for write").arg(path));
			f.write(QByteArray(bytes, '\0'));
		};

		writeText(surroundDir + "/main.txt",
			"# main\n"
			"Preamp: -3 dB\n"
			"Include: child.txt\n"
			"Convolution: ir.wav\n");
		writeText(surroundDir + "/child.txt",
			"Convolution: nested.wav\n");
		writeBlob(surroundDir + "/ir.wav", 128);
		writeBlob(surroundDir + "/nested.wav", 64);

		EqAPO::Import::ImportManifest manifest = EqAPO::Import::ConfigDependencyScanner::scan(
			surroundDir + "/main.txt", tempDir.path() + "/configdir");

		expectFalse(manifest.hasErrors, "scan should not flag any errors for this tree");
		expectEqual(int(manifest.items.size()), 4, "expected root + child + ir + nested");

		expectEqual(manifest.items[0].kind, "Root", "first item must be the root config");
		expectEqual(manifest.items[0].destRelative, "Surround/main.txt", "root dest path");
		expectTrue(manifest.totalBytes > 0, "totalBytes should be positive");

		QStringList destRels;
		for (const auto& item : manifest.items)
			destRels.append(item.destRelative);
		expectTrue(destRels.contains("Surround/main.txt"), "root present in items");
		expectTrue(destRels.contains("Surround/child.txt"), "include child present");
		expectTrue(destRels.contains("Surround/ir.wav"), "ir wav present");
		expectTrue(destRels.contains("Surround/nested.wav"), "nested wav present");

		// Missing reference should surface as a non-fatal warning + hasErrors.
		writeText(surroundDir + "/broken.txt", "Convolution: does_not_exist.wav\n");
		auto broken = EqAPO::Import::ConfigDependencyScanner::scan(surroundDir + "/broken.txt", tempDir.path() + "/configdir");
		expectTrue(broken.hasErrors, "missing dependency must flag hasErrors");
		expectTrue(!broken.warnings.isEmpty(), "missing dependency must produce a warning");

		QString configDest = tempDir.path() + "/configdir";
		EqAPO::Import::ExecutionResult exec = EqAPO::Import::ImportExecutor::execute(manifest, configDest);
		expectTrue(exec.success, "executor should succeed on clean manifest");
		expectEqual(exec.filesCopied, 4, "executor must copy four files");

		expectTrue(QFile::exists(configDest + "/Surround/main.txt"), "main.txt missing after import");
		expectTrue(QFile::exists(configDest + "/Surround/child.txt"), "child.txt missing after import");
		expectTrue(QFile::exists(configDest + "/Surround/ir.wav"), "ir.wav missing after import");
		expectTrue(QFile::exists(configDest + "/Surround/nested.wav"), "nested.wav missing after import");

		// Re-executing should be idempotent (overwrites are allowed).
		EqAPO::Import::ExecutionResult exec2 = EqAPO::Import::ImportExecutor::execute(manifest, configDest);
		expectTrue(exec2.success, "second execute should also succeed");
		expectEqual(exec2.filesCopied, 4, "second execute should still report four copies");

		// A bare impulse-response file - the path the ConvolutionCardEditor
		// import button takes - scans to a single-item manifest rooted at the
		// file itself (no .txt recursion), keeping the source folder name as a
		// subdirectory so the copy lands at config/<folder>/<file>.
		QString convConfigDest = tempDir.path() + "/conv-configdir";
		EqAPO::Import::ImportManifest single = EqAPO::Import::ConfigDependencyScanner::scan(
			surroundDir + "/ir.wav", convConfigDest);
		expectFalse(single.hasErrors, "single wav scan should not flag errors");
		expectEqual(int(single.items.size()), 1, "single wav scan yields exactly one item");
		expectEqual(single.items[0].kind, "Root", "single wav item is the root");
		expectEqual(single.items[0].destRelative, "Surround/ir.wav", "single wav keeps its source folder");
		expectEqual(single.rootDest, "Surround/ir.wav", "single wav rootDest mirrors the item");

		EqAPO::Import::ExecutionResult singleExec = EqAPO::Import::ImportExecutor::execute(single, convConfigDest);
		expectTrue(singleExec.success, "single wav import should succeed");
		expectEqual(singleExec.filesCopied, 1, "single wav import copies exactly one file");
		expectTrue(QFile::exists(convConfigDest + "/Surround/ir.wav"), "ir.wav missing after single-file import");
	}

	{
		// ChannelSelectionModel serialization identity: for equivalent
		// selections the in-place chip editor must write the same bytes the
		// legacy multi-select dialog produced (standard positions in the
		// dialog's checkbox order, then non-standard device channels, then
		// custom names).
		const std::vector<std::wstring> stereo = { L"L", L"R" };
		const std::vector<std::wstring> surround51 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR" };
		const std::vector<std::wstring> surround71 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" };

		ChannelSelectionModel model;
		model.load("L R C", surround51);
		expectEqual(model.serialize(), "C L R", "5.1 selection must serialize in dialog order");

		model.load("R L", stereo);
		expectEqual(model.serialize(), "L R", "written order canonicalizes like the dialog");

		// Position numbers resolve against the device order (engine
		// semantics, ChannelHelper::getChannelIndex), and are written back
		// as names like the dialog did.
		model.load("2", stereo);
		expectEqual(model.serialize(), "R", "numeric selector resolves in device order");

		// Historical aliases follow the engine: SUB -> LFE, SL <-> RL.
		model.load("SUB", surround51);
		expectEqual(model.serialize(), "LFE", "SUB alias selects the LFE chip");
		model.load("SL", surround51);
		expectEqual(model.serialize(), "RL", "SL on a back-channel device selects RL");

		model.load("SR SL LFE", surround71);
		expectEqual(model.serialize(), "SL SR LFE", "7.1 selection serializes in dialog order");

		// ALL wins over individual selections, exactly like the dialog.
		model.load("ALL L", surround51);
		expectTrue(model.allSelected(), "ALL token sets the all-channels state");
		expectEqual(model.serialize(), "ALL", "ALL serializes alone");

		// Custom/virtual channels keep their written order after the device
		// chips, matching the dialog's list section.
		model.load("VSL L VSR", stereo);
		expectEqual(model.serialize(), "L VSL VSR", "custom names follow device channels");
		model.toggle("R");
		expectEqual(model.serialize(), "L R VSL VSR", "toggling keeps canonical order");
		model.toggle("L");
		expectEqual(model.serialize(), "R VSL VSR", "deselecting removes the token");

		expectFalse(model.addCustom("  "), "blank custom name is rejected");
		expectFalse(model.addCustom("A B"), "multi-token custom name is rejected");
		expectTrue(model.addCustom(" vrr "), "custom name is trimmed and accepted");
		expectEqual(model.serialize(), "R VSL VSR VRR", "added custom name serializes upper-cased");

		// addCustom resolves aliases against the device set too: SUB selects
		// the LFE chip instead of duplicating it as a custom name.
		model.load("", surround51);
		expectTrue(model.addCustom("sub"), "SUB through addCustom is accepted");
		expectEqual(model.serialize(), "LFE", "SUB resolves to the device's LFE chip");
	}

	{
		// DeviceSelectionModel serialization identity: for equivalent device
		// selections the in-place chip editor must write the same bytes the
		// legacy change-button dialog produced - "all", or each selected
		// device's device string joined with "; " in list order (output
		// devices first, then input). Matching runs through the shared
		// DeviceCommand codec, the same one the engine uses, so a chip is
		// pre-selected exactly when the engine would match that device.
		auto dev = [](const QString& deviceString, const QString& name, bool installed, bool isInput) {
			DeviceEntry e;
			e.deviceString = deviceString;
			e.name = name;
			e.installed = installed;
			e.isInput = isInput;
			return e;
		};
		const QString devSpeakers = "Speakers Realtek HD Audio {0.0.0.00000000}.{aaaaaaaa-1111-2222-3333-444444444444}";
		const QString devHeadphones = "Headphones Realtek HD Audio {0.0.0.00000000}.{bbbbbbbb-1111-2222-3333-444444444444}";
		const QString devDigital = "Digital Output Realtek HD Audio {0.0.0.00000000}.{cccccccc-1111-2222-3333-444444444444}";
		const QString devMic = "Microphone Realtek HD Audio {0.0.1.00000000}.{dddddddd-1111-2222-3333-444444444444}";
		const QList<DeviceEntry> devices = {
			dev(devSpeakers, "Speakers", true, false),
			dev(devHeadphones, "Headphones", true, false),
			dev(devDigital, "Digital Output", false, false),
			dev(devMic, "Microphone", true, true),
		};

		DeviceSelectionModel model;

		// The literal lowercase "all" line is the all-devices state, like the
		// dialog's "All devices" choice: it round-trips to "all" and marks no
		// individual chip selected.
		model.load("all", devices);
		expectTrue(model.allSelected(), "literal 'all' sets the all-devices state");
		expectEqual(model.serialize(), "all", "all-devices serializes back as 'all'");

		// An empty parameter is not the all state and selects nothing.
		model.load("", devices);
		expectFalse(model.allSelected(), "empty parameter is not the all state");
		expectEqual(model.serialize(), "", "no selection serializes empty");

		// A full device-string pattern pre-selects exactly that endpoint and
		// round-trips byte-for-byte, GUID included.
		model.load(devSpeakers, devices);
		expectFalse(model.allSelected(), "a specific device is not the all state");
		expectEqual(model.serialize(), devSpeakers, "single device round-trips verbatim");

		// A bare word matches as a case-insensitive substring (DeviceCommand
		// semantics) and is rewritten to the matched device's full string.
		model.load("headphones", devices);
		expectEqual(model.serialize(), devHeadphones, "word pattern selects and canonicalizes to the device string");

		// Multiple patterns, written input-first, serialize in list order
		// (output devices first, then input) joined with "; ".
		model.load(devMic + "; " + devSpeakers, devices);
		expectEqual(model.serialize(), devSpeakers + "; " + devMic, "multiple devices serialize in list order");

		// toggle() flips one chip and keeps canonical list order.
		model.load(devSpeakers, devices);
		model.toggle(devHeadphones);
		expectEqual(model.serialize(), devSpeakers + "; " + devHeadphones, "toggling on adds a chip in list order");
		model.toggle(devSpeakers);
		expectEqual(model.serialize(), devHeadphones, "toggling off removes the token");

		// "All devices" wins over individual selections, like the dialog.
		model.load(devSpeakers, devices);
		model.setAllSelected(true);
		expectEqual(model.serialize(), "all", "All overrides individual selections");
	}

	{
		// MultiConvolutionRoutingAdapter: mappings <-> the routing views'
		// Assignment type must round-trip, because the card serializes the
		// edited view back into the config line. IR channels ride as decimal
		// summand channels at unity factor.
		using Mapping = MultiConvolutionCommand::Mapping;

		const std::vector<Mapping> brir = {{L"L", {0, 1}}, {L"R", {2, 3}}};
		std::vector<Assignment> assignments = MultiConvolutionRoutingAdapter::toAssignments(brir, 4);
		expectEqual((int)assignments.size(), 2, "two mappings become two assignments");
		expectTrue(assignments.size() == 2
			&& assignments[0].targetChannel == L"L" && assignments[0].sourceSum.size() == 2
			&& assignments[0].sourceSum[0].channel == L"0" && assignments[0].sourceSum[1].channel == L"1"
			&& assignments[0].sourceSum[0].factor == 1.0 && !assignments[0].sourceSum[0].isDecibel,
			"IR channels become decimal summands at unity factor");

		std::vector<Mapping> roundTrip = MultiConvolutionRoutingAdapter::toMappings(assignments);
		expectTrue(roundTrip.size() == 2
			&& roundTrip[0].targetChannel == L"L" && roundTrip[0].irChannels == std::vector<unsigned>({0, 1})
			&& roundTrip[1].targetChannel == L"R" && roundTrip[1].irChannels == std::vector<unsigned>({2, 3}),
			"assignments convert back to the same mappings");

		// The simple form expands to every file channel for display, and to
		// nothing when the channel count is unknown (callers must not offer
		// editing then).
		std::vector<Assignment> expanded = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 3);
		expectTrue(expanded.size() == 1 && expanded[0].sourceSum.size() == 3
			&& expanded[0].sourceSum[2].channel == L"2",
			"the simple form expands to every file channel");
		std::vector<Assignment> unknown = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 0);
		expectTrue(unknown.size() == 1 && unknown[0].sourceSum.empty(),
			"an unknown channel count expands to nothing");

		// Seeded placeholder rows (empty sums) and non-numeric summands are
		// dropped on the way back, like Copy's serializer skips empty rows.
		std::vector<Assignment> edited = assignments;
		Assignment seeded;
		seeded.targetChannel = L"C";
		edited.push_back(seeded);
		Assignment::Summand bogus;
		bogus.factor = 1.0;
		bogus.channel = L"VSL";
		edited[0].sourceSum.push_back(bogus);
		std::vector<Mapping> cleaned = MultiConvolutionRoutingAdapter::toMappings(edited);
		expectTrue(cleaned.size() == 2 && cleaned[0].irChannels == std::vector<unsigned>({0, 1}),
			"placeholder rows and non-numeric summands are dropped");

		// Source ports: "0".."N-1" from the file, then any referenced index
		// beyond the file so stale connections stay visible and removable.
		QStringList ports = MultiConvolutionRoutingAdapter::sourcePorts(2, {{L"L", {0, 7}}});
		expectEqual(ports.join(','), QString("0,1,7"), "ports are the file channels plus stale references");
		QStringList portsNoFile = MultiConvolutionRoutingAdapter::sourcePorts(0, {{L"L", {2, 1}}});
		expectEqual(portsNoFile.join(','), QString("1,2"), "without a file only referenced indices appear, sorted");
	}

	harness.report();
	return 0;
}
