/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

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
// Constitution: docs/skins/studio.md
SkinTokens studioTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
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
		t.background = QStringLiteral("#070A12");
		t.surface = QStringLiteral("#0D1322");
		t.card = QStringLiteral("#121A2C");
		t.cardHover = QStringLiteral("#182238");
		t.cardSelected = QStringLiteral("#1E3158");
		t.text = QStringLiteral("#E8EEFB");
		t.mutedText = QStringLiteral("#91A0BA");
		t.border = QStringLiteral("#26324A");
		t.graph = QStringLiteral("#060914");
		t.graphGridMinor = QStringLiteral("#26324A");
		t.accent = QStringLiteral("#5B8CFF");
		t.accent2 = QStringLiteral("#A66CFF");
	}
	else
	{
		t.background = QStringLiteral("#EEF2F8");
		t.surface = QStringLiteral("#F8FAFE");
		t.card = QStringLiteral("#FFFFFF");
		t.cardHover = QStringLiteral("#F3F6FC");
		t.cardSelected = QStringLiteral("#DDE8FF");
		t.text = QStringLiteral("#182033");
		t.mutedText = QStringLiteral("#5D6A84");
		t.border = QStringLiteral("#BCC8DE");
		t.graph = QStringLiteral("#F6F7FB");
		t.graphGridMinor = QStringLiteral("#D8E0EF");
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
	t.fontFamily = QStringLiteral("DM Mono");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 0;
	t.rowHeight = 32;
	t.channelGroupIndent = 16;
	t.channelGroupStyle = SkinTokens::TreeLines;
	t.badgeStyle = SkinTokens::OutlineOnly;
	t.zebraStripe = true;
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
		t.graphGridMajor = QStringLiteral("#383838");
		t.graphGridMinor = QStringLiteral("#2c2c2c");
	}
	else
	{
		t.background = QStringLiteral("#F6F6F3");
		t.surface = QStringLiteral("#FFFFFF");
		t.card = QStringLiteral("#FFFFFF");
		t.cardHover = QStringLiteral("#F0F0EC");
		t.cardSelected = QStringLiteral("#E8F1FF");
		t.text = QStringLiteral("#202020");
		t.mutedText = QStringLiteral("#666660");
		t.border = QStringLiteral("#D2D2CC");
		t.graph = QStringLiteral("#FFFFFF");
		t.graphGridMajor = QStringLiteral("#D2D2CC");
		t.graphGridMinor = QStringLiteral("#E6E6E0");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/soft.md
SkinTokens softTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 14;
	// 44, down from 48 (2026-08 density round): still the airiest header of
	// the five, but no longer half again the compact skins' height.
	t.rowHeight = 44;
	t.channelGroupIndent = 20;
	t.density = 2;
	t.channelGroupStyle = SkinTokens::SoftShadow;
	t.badgeStyle = SkinTokens::SoftPill;
	// The accent and the semantic colours live on the pastel shelf
	// themselves (the softPastelize recipe applied to the old saturated
	// values), so every consumer - knob arcs, focus rings, toggles, ON
	// pills, severity inks - is pastel without knowing it.
	if (dark)
	{
		t.background = QStringLiteral("#1C1A17");
		t.surface = QStringLiteral("#262320");
		t.card = QStringLiteral("#2F2B26");
		t.cardHover = QStringLiteral("#38332D");
		// The pastel accent mixed deep into the card (softMix 0.75).
		t.cardSelected = QStringLiteral("#3F4650");
		t.text = QStringLiteral("#F4F1EA");
		t.mutedText = QStringLiteral("#B3AB9D");
		t.border = QStringLiteral("#423D34");
		t.graph = QStringLiteral("#181613");
		t.accent = QStringLiteral("#6E96CF");
		t.accent2 = QStringLiteral("#8B6ECF");
		t.success = QStringLiteral("#6ECF91");
		t.warning = QStringLiteral("#CFAB6E");
		t.danger = QStringLiteral("#CF6E6E");
	}
	else
	{
		t.background = QStringLiteral("#F7F4EF");
		t.surface = QStringLiteral("#FFFDF9");
		t.card = QStringLiteral("#FFFFFF");
		t.cardHover = QStringLiteral("#FFF7EC");
		t.cardSelected = QStringLiteral("#EEF2FF");
		t.text = QStringLiteral("#28231F");
		t.mutedText = QStringLiteral("#786F67");
		t.border = QStringLiteral("#E9DED1");
		t.graph = QStringLiteral("#FFFAF3");
		t.accent = QStringLiteral("#6190D1");
		t.accent2 = QStringLiteral("#8361D1");
		t.success = QStringLiteral("#61D18A");
		t.warning = QStringLiteral("#D1A861");
		t.danger = QStringLiteral("#D16161");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/rack.md
SkinTokens rackTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
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
		t.background = QStringLiteral("#E7E2D8");
		t.surface = QStringLiteral("#F4EFE5");
		t.card = QStringLiteral("#FFFAEF");
		t.cardHover = QStringLiteral("#F7EEDC");
		t.cardSelected = QStringLiteral("#FCE8BD");
		t.text = QStringLiteral("#2B2721");
		t.mutedText = QStringLiteral("#746A5D");
		t.border = QStringLiteral("#C9BFAE");
		t.graph = QStringLiteral("#FFF7E6");
		t.graphGridMinor = QStringLiteral("#D6C4A6");
	}
	finishTokens(t);
	return t;
}

// Constitution: docs/skins/matrix.md
SkinTokens matrixTokens(bool dark)
{
	SkinTokens t;
	t.dark = dark;
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
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
		t.background = QStringLiteral("#F0F6F8");
		t.surface = QStringLiteral("#FFFFFF");
		t.card = QStringLiteral("#F9FCFD");
		t.cardHover = QStringLiteral("#EDF7FA");
		t.cardSelected = QStringLiteral("#D7F8FF");
		t.text = QStringLiteral("#10242F");
		t.mutedText = QStringLiteral("#5F7782");
		t.border = QStringLiteral("#D4E2E8");
		t.graph = QStringLiteral("#F9FCFD");
		t.graphGridMinor = QStringLiteral("#D4E2E8");
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
	static bool commonAdded = false;
	static bool sarasaAdded = false;
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

	const QStringList cjkChain = {
		QStringLiteral("Pretendard"), QStringLiteral("Noto Sans KR"),
		QStringLiteral("Noto Sans"), QStringLiteral("Malgun Gothic"),
		QStringLiteral("Microsoft YaHei")
	};
	QFont::insertSubstitutions(QStringLiteral("DM Sans"), cjkChain);
	QStringList monoChain{ QStringLiteral("Consolas") };
	if (includeSarasa)
		monoChain.append(QStringLiteral("Sarasa Mono K"));
	QFont::insertSubstitutions(QStringLiteral("DM Mono"), monoChain + cjkChain);
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
		{ QStringLiteral("soft"), QStringLiteral("soft"), &softTokens },
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
	// The two aliases are stored values from earlier releases, so they cannot be
	// derived from anything - they are the only hard-coded ids here.
	if (id == QStringLiteral("glassy"))
		return QStringLiteral("studio");
	if (id == QStringLiteral("industrial"))
		return QStringLiteral("rack");
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
		{ "@FONT@", tokens.fontFamily },
		{ "@MONO@", tokens.monoFontFamily }
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
