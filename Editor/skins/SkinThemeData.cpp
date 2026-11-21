/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Skin theme data (see SkinThemeData.h). The skin classes' tokens()
	overrides delegate here, so this file is the single source of truth for
	skin colours.
*/

#include "SkinThemeData.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>

// finishTokens; header-only, no link dependency on the skin classes.
#include "shared/SkinSupport.h"

namespace
{
// The fallback families behind a skin's body face, in the order the sheets'
// @FONT_STACK@ lists them. The same list is the substitution chain of
// "EAPO Sans" (registerBundledFonts), so the sheets and the code cannot
// drift apart.
const QStringList& bodyFallbackFamilies()
{
	static const QStringList families = {
		QStringLiteral("EAPO Sans KR"), QStringLiteral("Noto Sans KR"),
		QStringLiteral("Noto Sans"), QStringLiteral("Malgun Gothic"),
		QStringLiteral("Microsoft YaHei")
	};
	return families;
}

// The fallback families behind a skin's monospace face, in the order the
// sheets' @MONO_STACK@ lists them. The substitution chain of "EAPO Mono"
// (registerBundledFonts) follows the same order; it used to put Consolas
// first, and the gallery renders identically either way (audit #348 F8).
const QStringList& monoFallbackFamilies()
{
	static const QStringList families = {
		QStringLiteral("EAPO Mono K"), QStringLiteral("EAPO Sans KR"),
		QStringLiteral("Consolas"), QStringLiteral("Malgun Gothic")
	};
	return families;
}

// A QSS font-family value: the skin's own face first, then the fallbacks,
// then the generic family.
QString fontStack(const QString& family, const QStringList& fallbacks, const QString& generic)
{
	QStringList quoted{ QStringLiteral("\"%1\"").arg(family) };
	for (const QString& fallback : fallbacks)
		quoted.append(QStringLiteral("\"%1\"").arg(fallback));
	quoted.append(generic);
	return quoted.join(QStringLiteral(", "));
}

// Constitution: docs/skins/studio.md
SkinTokens studioTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("EAPO Sans");
	t.monoFontFamily = QStringLiteral("EAPO Mono");
	t.borderRadius = 8;
	// 36, down from 40 (2026-08 density round): a long config only ever
	// shows a handful of cards per screen, and the header floor was the
	// binding height on every collapsed row.
	t.rowHeight = 36;
	t.channelGroupIndent = 18;
	t.channelGroupStyle = SkinTokens::GradientBar;
	t.badgeStyle = SkinTokens::ColorPill;
	if (dark)
	{
		t.background = QStringLiteral("#05060C");
		t.surface = QStringLiteral("#080C16");
		t.card = QStringLiteral("#0C111D");
		t.cardHover = QStringLiteral("#101624");
		t.cardSelected = QStringLiteral("#142039");
		t.text = QStringLiteral("#E8EEFB");
		t.mutedText = QStringLiteral("#91A0BA");
		t.border = QStringLiteral("#192030");
		t.graph = QStringLiteral("#04060D");
		t.graphGridMinor = QStringLiteral("#192030");
		t.accent = QStringLiteral("#5B8CFF");
		t.accent2 = QStringLiteral("#A66CFF");
	}
	else
	{
		t.background = QStringLiteral("#D0DBEC");
		t.surface = QStringLiteral("#C8D8F7");
		t.card = QStringLiteral("#DCE0E8");
		t.cardHover = QStringLiteral("#CCD8F2");
		t.cardSelected = QStringLiteral("#BBD1FF");
		t.text = QStringLiteral("#182033");
		t.mutedText = QStringLiteral("#5D6A84");
		t.border = QStringLiteral("#BCC8DE");
		t.graph = QStringLiteral("#D3D8EB");
		t.graphGridMinor = QStringLiteral("#CCD6EA");
		t.accent = QStringLiteral("#2F6BFF");
		t.accent2 = QStringLiteral("#8A4DFF");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/minimal.md
SkinTokens minimalTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.accent = QStringLiteral("#3B82F6");
	t.fontFamily = QStringLiteral("EAPO Mono");
	t.monoFontFamily = QStringLiteral("EAPO Mono");
	t.borderRadius = 0;
	t.rowHeight = 32;
	t.channelGroupIndent = 16;
	t.channelGroupStyle = SkinTokens::TreeLines;
	t.badgeStyle = SkinTokens::OutlineOnly;
	if (dark)
	{
		t.background = QStringLiteral("#191919");
		t.surface = QStringLiteral("#1f1f1f");
		t.card = QStringLiteral("#262626");
		t.cardHover = QStringLiteral("#2c2c2c");
		t.cardSelected = QStringLiteral("#2A4878");
		t.text = QStringLiteral("#cccccc");
		t.mutedText = QStringLiteral("#777777");
		t.border = QStringLiteral("#3c3c3c");
		t.graph = QStringLiteral("#0e0e0e");
		t.graphGridMinor = QStringLiteral("#2c2c2c");
	}
	else
	{
		t.background = QStringLiteral("#E2E2E2");
		t.surface = QStringLiteral("#E0E0E0");
		t.card = QStringLiteral("#E0E0E0");
		t.cardHover = QStringLiteral("#DADADA");
		t.cardSelected = QStringLiteral("#BDD7FF");
		t.text = QStringLiteral("#202020");
		t.mutedText = QStringLiteral("#656565");
		t.border = QStringLiteral("#D1D1D1");
		t.graph = QStringLiteral("#E0E0E0");
		t.graphGridMinor = QStringLiteral("#DEDEDE");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/flat.md
SkinTokens flatTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	// Pretendard for every script, so Latin, digits and Hangul share one face;
	// source text (command lines, conditions) is set in the Sarasa mono.
	t.fontFamily = QStringLiteral("EAPO Sans KR");
	t.monoFontFamily = QStringLiteral("EAPO Mono K");
	t.borderRadius = 10;
	t.rowHeight = 44;
	t.channelGroupIndent = 20;
	// The group and badge kinds only pick the shared fallbacks; the skin
	// paints its own scope gutter and channel tags.
	t.channelGroupStyle = SkinTokens::TreeLines;
	t.badgeStyle = SkinTokens::OutlineOnly;
	// Neutral grounds, a white (or near-black) surface for cards, and one
	// emphasis colour: ink in the light theme, a blue in the dark one. The
	// accent never tints an area, so cardSelected is a neutral step, not a
	// mix of the accent. accent2 is the accent: nothing here is told apart
	// by a second hue.
	if (dark)
	{
		t.background = QStringLiteral("#0F1012");
		t.surface = QStringLiteral("#17191C");
		t.card = QStringLiteral("#17191C");
		t.cardHover = QStringLiteral("#1F2226");
		t.cardSelected = QStringLiteral("#24282D");
		t.text = QStringLiteral("#E7E8EA");
		t.mutedText = QStringLiteral("#9EA2A8");
		t.border = QStringLiteral("#2A2D31");
		t.borderStrong = QStringLiteral("#3B3F45");
		t.controlEdge = QStringLiteral("#6F747C");
		t.graph = QStringLiteral("#131517");
		t.accent = QStringLiteral("#8CB3ED");
		t.accent2 = QStringLiteral("#8CB3ED");
		t.onAccent = QStringLiteral("#0F1216");
		t.success = QStringLiteral("#63C27C");
		t.warning = QStringLiteral("#E3A03A");
		t.danger = QStringLiteral("#FF7C62");
	}
	else
	{
		t.background = QStringLiteral("#F6F6F5");
		t.surface = QStringLiteral("#FFFFFF");
		t.card = QStringLiteral("#FFFFFF");
		t.cardHover = QStringLiteral("#F1F1EF");
		t.cardSelected = QStringLiteral("#EAEAE7");
		t.text = QStringLiteral("#1C1C1B");
		t.mutedText = QStringLiteral("#5D5D59");
		t.border = QStringLiteral("#E2E2DF");
		t.borderStrong = QStringLiteral("#C9C9C5");
		t.controlEdge = QStringLiteral("#8A8A85");
		t.graph = QStringLiteral("#F1F1EF");
		t.accent = QStringLiteral("#1C1C1B");
		t.accent2 = QStringLiteral("#1C1C1B");
		t.onAccent = QStringLiteral("#FFFFFF");
		t.success = QStringLiteral("#2E7D46");
		t.warning = QStringLiteral("#965A00");
		t.danger = QStringLiteral("#B42D14");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/rack.md
SkinTokens rackTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("EAPO Sans");
	t.monoFontFamily = QStringLiteral("EAPO Mono");
	t.borderRadius = 3;
	t.rowHeight = 36;
	t.channelGroupIndent = 16;
	t.channelGroupStyle = SkinTokens::DottedLine;
	t.badgeStyle = SkinTokens::WireframeBorder;
	t.accent = dark ? QStringLiteral("#F4B860") : QStringLiteral("#B66A00");
	t.accent2 = dark ? QStringLiteral("#5ED0A0") : QStringLiteral("#177A55");
	if (dark)
	{
		t.background = QStringLiteral("#0B0D0F");
		t.surface = QStringLiteral("#14181C");
		t.card = QStringLiteral("#1D2328");
		t.cardHover = QStringLiteral("#252B2F");
		t.cardSelected = QStringLiteral("#332718");
		t.text = QStringLiteral("#E6E0D4");
		t.mutedText = QStringLiteral("#9A9488");
		t.border = QStringLiteral("#3A4248");
		t.graph = QStringLiteral("#060807");
		t.graphGridMinor = QStringLiteral("#1F3A31");
	}
	else
	{
		t.background = QStringLiteral("#E2DDD3");
		t.surface = QStringLiteral("#E4DFD5");
		t.card = QStringLiteral("#E7E2D7");
		t.cardHover = QStringLiteral("#EAE1CF");
		t.cardSelected = QStringLiteral("#F9E5BA");
		t.text = QStringLiteral("#2B2721");
		t.mutedText = QStringLiteral("#746A5D");
		t.border = QStringLiteral("#C9BFAE");
		t.graph = QStringLiteral("#EAE2D1");
		t.graphGridMinor = QStringLiteral("#D6C4A6");
		// The shared default warning (#f59e0b) is a lamp colour: as ink on
		// the cream plate it reaches 1.3:1 where the plate shades down.
		// This one holds 4.5:1 there. The dark finish keeps the default.
		t.warning = QStringLiteral("#7A4600");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/matrix.md
SkinTokens matrixTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("EAPO Sans");
	t.monoFontFamily = QStringLiteral("EAPO Mono");
	t.borderRadius = 0;
	t.rowHeight = 36;
	t.channelGroupIndent = 24;
	t.channelGroupStyle = SkinTokens::GradientBar;
	t.badgeStyle = SkinTokens::OutlineOnly;
	t.cardRailWidth = 3;
	t.accent = dark ? QStringLiteral("#22D3EE") : QStringLiteral("#008EAA");
	t.accent2 = dark ? QStringLiteral("#7CFFB2") : QStringLiteral("#0A8F57");
	if (dark)
	{
		t.background = QStringLiteral("#060B10");
		t.surface = QStringLiteral("#0B141C");
		t.card = QStringLiteral("#101B25");
		t.cardHover = QStringLiteral("#142432");
		t.cardSelected = QStringLiteral("#082B34");
		t.text = QStringLiteral("#DFF5FF");
		t.mutedText = QStringLiteral("#7FA0AE");
		t.border = QStringLiteral("#233443");
		t.graph = QStringLiteral("#041018");
		t.graphGridMinor = QStringLiteral("#183443");
	}
	else
	{
		t.background = QStringLiteral("#DCEAEF");
		t.surface = QStringLiteral("#E4EDF0");
		t.card = QStringLiteral("#DEEEF4");
		t.cardHover = QStringLiteral("#D7EDF4");
		t.cardSelected = QStringLiteral("#C4F5FF");
		t.text = QStringLiteral("#10242F");
		t.mutedText = QStringLiteral("#5F7782");
		t.border = QStringLiteral("#D0E0E6");
		t.graph = QStringLiteral("#DEEEF4");
		t.graphGridMinor = QStringLiteral("#D0E0E6");
		// Traffic-light status colours tuned for contrast on light surfaces.
		t.success = QStringLiteral("#15803D");
		t.warning = QStringLiteral("#B45309");
		t.danger = QStringLiteral("#DC2626");
	}
	finishTokens(t);
	return t;
}
}

namespace SkinThemeData
{
void registerBundledFonts(bool includeSarasa)
{
	// Guarded per QApplication, not per process: the Editor restarts inside
	// one process (interface-mode and language switches), and destroying the
	// QApplication drops every registered application font while a plain
	// static guard stayed true - the run after a legacy-rows round trip came
	// back with DM Sans / DM Mono / Pretendard missing and every skin fell
	// back to the system font. The guard is cleared by the application's
	// destroyed() signal. Comparing QApplication addresses (the previous fix)
	// did not work: main.cpp's restart loop builds the next QApplication in
	// the same stack slot, so the new instance had the old address and the
	// fonts were skipped (audit #348 TD-15).
	static bool commonAdded = false;
	static bool sarasaAdded = false;
	static bool resetConnected = false;
	if (!resetConnected && QCoreApplication::instance() != nullptr)
	{
		QObject::connect(QCoreApplication::instance(), &QObject::destroyed, [] {
			commonAdded = false;
			sarasaAdded = false;
			resetConnected = false;
		});
		resetConnected = true;
	}
	if (!commonAdded)
	{
		commonAdded = true;
		const QStringList fonts = {
			QStringLiteral(":/fonts/DMSans-Regular.ttf"),
			QStringLiteral(":/fonts/DMSans-Medium.ttf"),
			QStringLiteral(":/fonts/DMSans-SemiBold.ttf"),
			QStringLiteral(":/fonts/DMSans-Bold.ttf"),
			QStringLiteral(":/fonts/DMMono-Regular.ttf"),
			QStringLiteral(":/fonts/DMMono-Medium.ttf"),
			QStringLiteral(":/fonts/DMMono-SemiBold.ttf"),
			QStringLiteral(":/fonts/DMMono-Bold.ttf"),
			QStringLiteral(":/fonts/Pretendard-Regular.otf"),
			QStringLiteral(":/fonts/Pretendard-Medium.otf"),
			QStringLiteral(":/fonts/Pretendard-SemiBold.otf"),
			QStringLiteral(":/fonts/Pretendard-Bold.otf")
		};
		for (const QString& font : fonts)
			QFontDatabase::addApplicationFont(font);
	}
	if (includeSarasa && !sarasaAdded)
	{
		sarasaAdded = true;
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SarasaMonoK-Regular.ttf"));
		QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SarasaMonoK-Bold.ttf"));
	}

	const QStringList& cjkChain = bodyFallbackFamilies();
	QFont::insertSubstitutions(QStringLiteral("EAPO Sans"), cjkChain);
	// The sheets' mono order, without the Sarasa face a satellite tool does not
	// register, then the rest of the body chain.
	QStringList monoChain;
	for (const QString& family : monoFallbackFamilies())
		if (includeSarasa || family != QStringLiteral("EAPO Mono K"))
			monoChain.append(family);
	for (const QString& family : cjkChain)
		if (!monoChain.contains(family))
			monoChain.append(family);
	QFont::insertSubstitutions(QStringLiteral("EAPO Mono"), monoChain);
}

const QVector<SkinEntry>& roster()
{
	// Display order. Studio is first, and that is load-bearing twice over: it is
	// the default skin and it is what an unknown id falls back to.
	static const QVector<SkinEntry> entries = {
		{ QStringLiteral("studio"), QStringLiteral("studio"), &studioTokens },
		// The minimal skin's sheets are precision_light.qss / precision_dark.qss;
		// the name predates the skin's rename and the files were left alone.
		{ QStringLiteral("minimal"), QStringLiteral("precision"), &minimalTokens },
		{ QStringLiteral("flat"), QStringLiteral("flat"), &flatTokens },
		{ QStringLiteral("rack"), QStringLiteral("rack"), &rackTokens },
		{ QStringLiteral("matrix"), QStringLiteral("matrix"), &matrixTokens },
	};
	return entries;
}

QStringList ids()
{
	QStringList result;
	result.reserve(roster().size());
	for (const SkinEntry& skin : roster())
		result.append(skin.id);
	return result;
}

const SkinEntry& entry(const QString& id)
{
	const QString resolved = resolveId(id);
	for (const SkinEntry& skin : roster())
	{
		if (skin.id == resolved)
			return skin;
	}
	// resolveId only ever returns a roster id, so this is unreachable; returning
	// the first entry rather than asserting keeps the fallback that has always
	// been the behaviour for an id nobody knows.
	return roster().first();
}

QString resolveId(const QString& id)
{
	// The aliases are stored values from earlier releases, so they cannot be
	// derived from anything - they are the only hard-coded ids here. "soft" is
	// what the Stock Flat skin was stored as up to v2.71: a registry that still
	// holds it opens that skin, and the next save writes "flat".
	if (id == QStringLiteral("glassy"))
		return QStringLiteral("studio");
	if (id == QStringLiteral("industrial"))
		return QStringLiteral("rack");
	if (id == QStringLiteral("soft"))
		return QStringLiteral("flat");
	for (const SkinEntry& skin : roster())
	{
		if (skin.id == id)
			return id;
	}
	return roster().first().id;
}

void applyToApplication(QApplication& app, const QString& skinId, bool dark,
	bool setFusionStyle, bool includeSarasa)
{
	registerBundledFonts(includeSarasa);
	if (setFusionStyle)
		app.setStyle(QStyleFactory::create(QStringLiteral("fusion")));

	const QString resolvedId = resolveId(skinId);
	const SkinTokens themeTokens = tokens(resolvedId, dark);
	QString styleSheet;
	QFile sheet(qssResource(resolvedId, dark));
	if (!sheet.open(QFile::ReadOnly) && resolvedId != QLatin1String("studio"))
		sheet.setFileName(qssResource(QStringLiteral("studio"), dark));
	if (sheet.isOpen() || sheet.open(QFile::ReadOnly))
		styleSheet = QString::fromUtf8(sheet.readAll());

	app.setPalette(palette(themeTokens, dark));
	app.setStyleSheet(substituteTokens(styleSheet, themeTokens)
		+ comboArrowOverride() + fileDialogOverride());
}

SkinTokens tokens(const QString& id, bool dark)
{
	return entry(id).tokens(dark);
}

SkinTokens heritageTokens()
{
	// The widget chrome itself comes from the native style, untouched by QSS;
	// these values only reach the custom painters. Studio donates the values
	// the block below does not overwrite (metrics, secondary hues).
	SkinTokens tokens = SkinThemeData::tokens(QStringLiteral("studio"), false);
	tokens.dark = false;
	tokens.background = QStringLiteral("#f0f0f0");
	tokens.surface = QStringLiteral("#ffffff");
	tokens.surfaceRaised = QStringLiteral("#f5f5f5");
	tokens.surfaceSunken = QStringLiteral("#e8e8e8");
	tokens.card = QStringLiteral("#ffffff");
	tokens.cardHover = QStringLiteral("#f0f6fc");
	tokens.text = QStringLiteral("#000000");
	tokens.mutedText = QStringLiteral("#606060");
	tokens.border = QStringLiteral("#adadad");
	tokens.graph = QStringLiteral("#ffffff");
	tokens.graphGridMajor = QStringLiteral("#c8c8c8");
	tokens.graphGridMinor = QStringLiteral("#e4e4e4");
	tokens.accent = QStringLiteral("#0078d7");
	tokens.accent2 = QStringLiteral("#2b88d8");
	tokens.focusRing = QStringLiteral("#0078d7");
	tokens.fontFamily = QStringLiteral("Segoe UI");
	tokens.monoFontFamily = QStringLiteral("Consolas");
	return tokens;
}

QString qssResource(const QString& id, bool dark)
{
	return QStringLiteral(":/skins/%1_%2.qss")
		.arg(entry(id).qssBaseName, dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

QString substituteTokens(QString qss, const SkinTokens& tokens)
{
	// Token sentinels intentionally use the @TOKEN@ form so they survive Qt's
	// style sheet parser intact (a literal '@' is not meaningful in QSS) and
	// stand out in the source files. Order does not matter because every
	// sentinel is unique.
	struct Substitution { const char* placeholder = nullptr; QString value; };
	const Substitution table[] = {
		{ "@BG@", tokens.background },
		{ "@SURFACE@", tokens.surface },
		{ "@SURFACE_RAISED@", tokens.surfaceRaised },
		{ "@SURFACE_SUNKEN@", tokens.surfaceSunken },
		{ "@CARD@", tokens.card },
		{ "@CARD_HOVER@", tokens.cardHover },
		{ "@CARD_SELECTED@", tokens.cardSelected },
		{ "@TEXT@", tokens.text },
		{ "@MUTED@", tokens.mutedText },
		{ "@BORDER@", tokens.border },
		{ "@GRAPH@", tokens.graph },
		{ "@GRID_MAJOR@", tokens.graphGridMajor },
		{ "@GRID_MINOR@", tokens.graphGridMinor },
		{ "@ACCENT@", tokens.accent },
		{ "@ACCENT2@", tokens.accent2 },
		{ "@SUCCESS@", tokens.success },
		{ "@WARNING@", tokens.warning },
		{ "@DANGER@", tokens.danger },
		{ "@FOCUS@", tokens.focusRing },
		{ "@BORDER_STRONG@", tokens.borderStrong },
		{ "@EDGE@", tokens.controlEdge },
		{ "@ON_ACCENT@", tokens.onAccent },
		{ "@FONT@", tokens.fontFamily },
		{ "@MONO@", tokens.monoFontFamily },
		// Whole font-family values: the token face plus the bundled fallbacks.
		{ "@FONT_STACK@", fontStack(tokens.fontFamily, bodyFallbackFamilies(), QStringLiteral("sans-serif")) },
		{ "@MONO_STACK@", fontStack(tokens.monoFontFamily, monoFallbackFamilies(), QStringLiteral("monospace")) }
	};
	for (const Substitution& s : table)
	{
		// The alpha form first, because "@ACCENT@" is a prefix of "@ACCENT_RGB@"
		// and replacing the shorter one first would leave "#7AA2F7_RGB".
		//
		// QSS has no variables and its rgba() takes three numbers, so a sheet that
		// wanted a token at 30% had to write the palette value out by hand - and
		// then a token change did not reach it. With this it can say
		// rgba(@ACCENT_RGB@, 0.30) instead.
		//
		// Only colour tokens get the form; a font family has no channels. A value
		// that does not parse as a colour is skipped rather than emitting
		// something that would make the whole rule invalid.
		const QColor color(s.value);
		if (color.isValid())
		{
			qss.replace(QLatin1String(s.placeholder).left(int(qstrlen(s.placeholder)) - 1) + QLatin1String("_RGB@"),
				QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue()));
		}
		qss.replace(QLatin1String(s.placeholder), s.value);
	}
	return qss;
}

QString comboArrowOverride()
{
	return QStringLiteral(
		"QComboBox::down-arrow,"
		"QComboBox[paramSelector=\"true\"]::down-arrow,"
		"QComboBox[filterSelector=\"true\"]::down-arrow {"
		" image: url(:/icons/modern/chevron-down.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }"
		"QAbstractSpinBox::up-arrow {"
		" image: url(:/icons/modern/chevron-up.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }"
		"QAbstractSpinBox::down-arrow {"
		" image: url(:/icons/modern/chevron-down.svg); width: 12px; height: 12px;"
		" border: none; background: transparent; }");
}

QString fileDialogOverride()
{
	return QStringLiteral(
		"QFileDialog QToolButton {"
		" padding: 2px; min-height: 0px; min-width: 0px; }");
}

QPalette palette(const SkinTokens& tokens, bool dark)
{
	QPalette palette;
	QColor background(tokens.background);
	QColor surface(tokens.surface);
	QColor card(tokens.card);
	QColor text(tokens.text);
	QColor accent(tokens.accent);
	palette.setColor(QPalette::Window, background);
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, surface);
	palette.setColor(QPalette::AlternateBase, card);
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::Button, card);
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::ToolTipBase, card);
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, dark ? QColor(QStringLiteral("#0c0c16")) : QColor(QStringLiteral("#ffffff")));
	palette.setColor(QPalette::PlaceholderText, QColor(tokens.mutedText));
	palette.setColor(QPalette::Light, card.lighter(120));
	palette.setColor(QPalette::Midlight, card.lighter(105));
	palette.setColor(QPalette::Mid, surface);
	palette.setColor(QPalette::Dark, background.darker(120));
	palette.setColor(QPalette::Shadow, background.darker(160));
	palette.setColor(QPalette::Link, accent);
	palette.setColor(QPalette::LinkVisited, accent.darker(110));
	return palette;
}
}
