/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The filter command catalog is the one table behind the card identities,
	the pictograms, the picker descriptions and the add-picker template
	roster. These tests pin the properties that used to fail silently when
	the vocabulary lived in four tables: a command without an icon, a
	template that does not parse as its command, a command missing from the
	picker, an icon resource naming a file that does not exist.
*/

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include "Editor/widgets/FilterCardModel.h"
#include "Editor/widgets/FilterCommandCatalog.h"
#include "Editor/helpers/VstChunkScan.h"

#include "EditorLogicTestSupport.h"

namespace
{
QString commandWordOfTemplate(const FilterCommandCatalog::TemplateEntry& entry)
{
	switch (entry.kind)
	{
	case FilterCommandCatalog::TemplateKind::GraphicEQBands15:
	case FilterCommandCatalog::TemplateKind::GraphicEQBands31:
		return QStringLiteral("GraphicEQ");
	case FilterCommandCatalog::TemplateKind::SubwooferRoutingDefaultState:
		return QStringLiteral("SubwooferRouting");
	case FilterCommandCatalog::TemplateKind::Literal:
		break;
	}
	const QString line = QLatin1String(entry.line);
	if (line.startsWith(QLatin1Char('#')))
		return QStringLiteral("#");
	const int colon = line.indexOf(QLatin1Char(':'));
	return (colon > 0 ? line.left(colon) : line).trimmed();
}
}

void testFilterCommandCatalogRoster()
{
	const QList<FilterCommandCatalog::CommandEntry>& entries = FilterCommandCatalog::commands();
	requireEqual(int(entries.size()), 22, "the catalog covers every decorated command");

	QSet<QString> keywords;
	for (const FilterCommandCatalog::CommandEntry& entry : entries)
	{
		const QString keyword = QLatin1String(entry.keyword);
		expectFalse(keywords.contains(keyword), "keyword appears once: " + keyword);
		keywords.insert(keyword);
		expectTrue(entry.type != nullptr && entry.type[0] != '\0', "type set for " + keyword);
		expectTrue(entry.badge != nullptr && entry.badge[0] != '\0', "badge set for " + keyword);
		expectTrue(entry.icon != nullptr && entry.icon[0] != '\0', "icon set for " + keyword);
		expectTrue(entry.title != nullptr && entry.title[0] != '\0', "title set for " + keyword);
		const QString color = QLatin1String(entry.color);
		expectTrue(color.startsWith(QLatin1Char('#')) && color.size() == 7,
			"accent is a #rrggbb hex for " + keyword);
	}

	// The lookups: canonical spelling is case-sensitive (a lowercase
	// "preamp:" is a note, matching the engine); the icon/picker word
	// lookup is case-insensitive and accepts the comment alias.
	expectTrue(FilterCommandCatalog::entryForKeyword(QStringLiteral("Preamp")) != nullptr,
		"canonical lookup finds Preamp");
	expectTrue(FilterCommandCatalog::entryForKeyword(QStringLiteral("preamp")) == nullptr,
		"canonical lookup stays case-sensitive");
	const FilterCommandCatalog::CommandEntry* upper
		= FilterCommandCatalog::entryForCommandWord(QStringLiteral("PREAMP"));
	requireTrue(upper != nullptr, "word lookup is case-insensitive");
	expectEqual(QLatin1String(upper->keyword), QStringLiteral("Preamp"), "word lookup lands on the row");
	const FilterCommandCatalog::CommandEntry* comment
		= FilterCommandCatalog::entryForCommandWord(QStringLiteral("comment"));
	requireTrue(comment != nullptr, "the comment alias resolves");
	expectEqual(QLatin1String(comment->keyword), QStringLiteral("#"), "comment alias lands on the # row");

	// The two deliberate type collisions: the convolution siblings and the
	// If family share one card type and split by badge.
	const FilterCommandCatalog::CommandEntry* conv = FilterCommandCatalog::entryForKeyword(QStringLiteral("Convolution"));
	const FilterCommandCatalog::CommandEntry* mconv = FilterCommandCatalog::entryForKeyword(QStringLiteral("MultiConvolution"));
	requireTrue(conv != nullptr && mconv != nullptr, "both convolution rows exist");
	expectEqual(QLatin1String(conv->type), QLatin1String(mconv->type), "convolution siblings share the card type");
	expectTrue(QLatin1String(conv->badge) != QLatin1String(mconv->badge), "convolution siblings split by badge");
	for (const char* branch : { "If", "ElseIf", "Else", "EndIf" })
	{
		const FilterCommandCatalog::CommandEntry* entry = FilterCommandCatalog::entryForKeyword(QLatin1String(branch));
		requireTrue(entry != nullptr, "If-family row exists");
		expectEqual(QLatin1String(entry->type), QStringLiteral("if"), "If family shares the card type");
	}
}

void testFilterCommandCatalogIconsExistOnDisk()
{
	// Resource paths are asserted as strings elsewhere; this pins that every
	// named SVG actually exists in the icon set, the drift class no gate
	// caught before (a catalog row naming a file the qrc never shipped).
	QDir repoRoot(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
	requireTrue(repoRoot.cdUp(), "catalog test reaches the tests directory");
	requireTrue(repoRoot.cdUp(), "catalog test reaches the repository root");

	auto expectIconFile = [&repoRoot](const char* baseName) {
		const QString path = repoRoot.filePath(
			QStringLiteral("Editor/icons/modern/%1.svg").arg(QLatin1String(baseName)));
		expectTrue(QFileInfo::exists(path), "icon file exists: " + path);
	};
	for (const FilterCommandCatalog::CommandEntry& entry : FilterCommandCatalog::commands())
		expectIconFile(entry.icon);
	for (const FilterCommandCatalog::BiquadCurveEntry& curve : FilterCommandCatalog::biquadCurves())
		expectIconFile(curve.icon);
}

void testFilterCommandCatalogTemplateRoster()
{
	const QList<FilterCommandCatalog::TemplateEntry>& templates = FilterCommandCatalog::pickerTemplates();
	requireEqual(int(templates.size()), 32, "the picker roster holds every template");

	// Every template resolves to a command the engine (or the comment card)
	// recognizes - a template whose line does not parse as its command would
	// insert a dead row.
	for (const FilterCommandCatalog::TemplateEntry& entry : templates)
	{
		const QString word = commandWordOfTemplate(entry);
		if (word == QStringLiteral("#"))
			continue;
		expectEqual(FilterCardModel::canonicalCommand(word), word,
			"template parses as its command: " + QString::fromUtf8(entry.name));
	}

	// Completeness the old four-table split could silently lose: every
	// catalogued command is reachable from the picker.
	QSet<QString> templateCommands;
	for (const FilterCommandCatalog::TemplateEntry& entry : templates)
		templateCommands.insert(commandWordOfTemplate(entry));
	for (const FilterCommandCatalog::CommandEntry& entry : FilterCommandCatalog::commands())
	{
		const QString keyword = QLatin1String(entry.keyword);
		expectTrue(templateCommands.contains(keyword),
			"command has a picker template: " + keyword);
	}

	// The section walk: grouped, in the intended order, with Control and
	// Branching closing the list. Translate() falls back to the source text
	// here because the test installs no translator.
	QStringList sectionOrder;
	for (const FilterCommandCatalog::TemplateEntry& entry : templates)
	{
		const QString section = FilterCommandCatalog::templatePath(entry).join(QStringLiteral(" / "));
		if (sectionOrder.isEmpty() || sectionOrder.last() != section)
			sectionOrder.append(section);
	}
	const QStringList expectedSections = {
		QString(),
		QStringLiteral("Basic filters"),
		QStringLiteral("Parametric filters"),
		QStringLiteral("Phase & Time"),
		QStringLiteral("Graphic equalizers"),
		QStringLiteral("Advanced filters"),
		QStringLiteral("Plugins"),
		QStringLiteral("Speaker management"),
		QStringLiteral("Control"),
		QStringLiteral("Branching")
	};
	expectEqual(sectionOrder, expectedSections,
		"sections appear grouped and in the intended order");
}

void testFilterCommandCatalogDescriptions()
{
	// Commands shipping without a per-command picker description: only
	// Filter, which describes per response curve instead. Every other
	// command must carry one - Hilbert and Velvet shipped without one until
	// the catalog made the gap visible.
	const QSet<QString> withoutDescription = { QStringLiteral("Filter") };
	for (const FilterCommandCatalog::CommandEntry& entry : FilterCommandCatalog::commands())
	{
		const QString keyword = QLatin1String(entry.keyword);
		if (withoutDescription.contains(keyword))
			expectTrue(FilterCommandCatalog::description(entry).isEmpty(),
				"description gap preserved for " + keyword);
		else
			expectFalse(FilterCommandCatalog::description(entry).isEmpty(),
				"description present for " + keyword);
	}

	requireEqual(int(FilterCommandCatalog::biquadCurves().size()), 8,
		"eight response curves");
	for (const FilterCommandCatalog::BiquadCurveEntry& curve : FilterCommandCatalog::biquadCurves())
		expectFalse(FilterCommandCatalog::curveDescription(curve).isEmpty(),
			"curve description present: " + QLatin1String(curve.code));
	expectEqual(QLatin1String(FilterCommandCatalog::biquadCurves().first().code),
		QStringLiteral("PK"), "PK leads the curve walk (badge fallback order)");
	expectFalse(FilterCommandCatalog::firstOrderAllPassDescription().isEmpty(),
		"the first-order all-pass has its dedicated wording");
}

void testSharedRawBodyAndRoutingViewPredicates()
{
	// The raw-body eligibility rule the five skins used to restate.
	expectTrue(FilterCardModel::hostsSharedRawBody(QStringLiteral("text"), false), "raw text hosts the raw body");
	expectTrue(FilterCardModel::hostsSharedRawBody(QStringLiteral("if"), false), "If family hosts the raw body");
	expectTrue(FilterCardModel::hostsSharedRawBody(QStringLiteral("eval"), false), "Eval hosts the raw body");
	expectTrue(FilterCardModel::hostsSharedRawBody(QStringLiteral("preamp"), true), "dynamic lines are eligible");
	expectFalse(FilterCardModel::hostsSharedRawBody(QStringLiteral("preamp"), false), "static editor rows are not");
	expectFalse(FilterCardModel::hostsSharedRawBody(QStringLiteral("copy"), false), "static Copy is not");

	// The Copy routing-view gate: static factors open the routing view,
	// inline-expression factors keep the raw body.
	const FilterCardDescriptor staticCopy = FilterCardModel::describeLine(QStringLiteral("Copy: L=R"));
	expectTrue(FilterCardModel::opensRoutingView(staticCopy), "a static Copy opens the routing view");
	const FilterCardDescriptor dynamicCopy = FilterCardModel::describeLine(QStringLiteral("Copy: L=`factor`*R"));
	expectTrue(dynamicCopy.dynamicLine, "inline expression detected on the Copy line");
	expectFalse(FilterCardModel::opensRoutingView(dynamicCopy), "a dynamic Copy keeps the raw body");
	const FilterCardDescriptor channel = FilterCardModel::describeLine(QStringLiteral("Channel: all"));
	expectFalse(FilterCardModel::opensRoutingView(channel), "only Copy opens the routing view");
}

void testVstChunkPathCandidates()
{
	// The scan decodes base64 state text and lifts absolute path spellings;
	// candidates keep duplicates and match order (dedup happens at the
	// warning stage, after the readability filter).
	const QString text = QStringLiteral(
		"prelude C:\\Samples\\My IR (final).wav middle "
		"D:\\Tools\\room.dll again C:\\Samples\\My IR (final).wav end");
	const QByteArray encoded = text.toUtf8().toBase64();
	const std::wstring chunk = QString::fromUtf8(encoded).toStdWString();

	const QStringList candidates = vstChunkPathCandidates(chunk);
	requireEqual(int(candidates.size()), 3, "three path spellings found");
	expectEqual(candidates[0], QStringLiteral("C:\\Samples\\My IR (final).wav"), "first path captured");
	expectEqual(candidates[1], QStringLiteral("D:\\Tools\\room.dll"), "second path captured");
	expectEqual(candidates[2], candidates[0], "duplicates preserved in candidate order");

	expectTrue(vstChunkPathCandidates(L"").isEmpty(), "an empty chunk yields nothing");
	expectTrue(vstChunkPathCandidates(std::wstring(100000, L'A')).isEmpty(),
		"oversized chunks are skipped, not scanned");
	const QByteArray noPaths = QStringLiteral("just settings, no paths").toUtf8().toBase64();
	expectTrue(vstChunkPathCandidates(QString::fromUtf8(noPaths).toStdWString()).isEmpty(),
		"pathless chunks yield nothing");

	// The warning list dedupes; with fixture paths that do not exist on
	// disk it stays empty, pinning the exists() gate.
	expectTrue(vstChunkUnreadablePaths(chunk).isEmpty(),
		"nonexistent candidates never reach the warning list");
}
