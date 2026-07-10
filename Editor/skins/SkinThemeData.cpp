/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Skin theme data (see SkinThemeData.h). The skin classes' tokens()
	overrides delegate here, so this file is the single source of truth for
	skin colours.
*/

#include "SkinThemeData.h"

// finishTokens; header-only, no link dependency on the skin classes.
#include "SkinSupport.h"

namespace
{
// Constitution: docs/skins/studio.md
SkinTokens studioTokens(bool dark)
{
	SkinTokens t;
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 8;
	t.rowHeight = 40;
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
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 14;
	t.rowHeight = 48;
	t.channelGroupIndent = 20;
	t.density = 2;
	t.channelGroupStyle = SkinTokens::SoftShadow;
	t.badgeStyle = SkinTokens::SoftPill;
	t.showRawPreview = false;
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
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 3;
	t.showRawPreview = false;
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
	t.fontFamily = QStringLiteral("DM Sans");
	t.monoFontFamily = QStringLiteral("DM Mono");
	t.borderRadius = 0;
	t.rowHeight = 36;
	t.channelGroupIndent = 24;
	t.channelGroupStyle = SkinTokens::GradientBar;
	t.badgeStyle = SkinTokens::OutlineOnly;
	t.cardRailWidth = 3;
	// The shared raw-preview strip is replaced by this skin's own caption
	// strip (MatrixRowCaption).
	t.showRawPreview = false;
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
QString resolveId(const QString& id)
{
	if (id == QStringLiteral("glassy"))
		return QStringLiteral("studio");
	if (id == QStringLiteral("industrial"))
		return QStringLiteral("rack");
	if (id == QStringLiteral("studio") || id == QStringLiteral("minimal") || id == QStringLiteral("soft")
		|| id == QStringLiteral("rack") || id == QStringLiteral("matrix"))
		return id;
	return QStringLiteral("studio");
}

SkinTokens tokens(const QString& id, bool dark)
{
	const QString resolved = resolveId(id);
	if (resolved == QStringLiteral("minimal"))
		return minimalTokens(dark);
	if (resolved == QStringLiteral("soft"))
		return softTokens(dark);
	if (resolved == QStringLiteral("rack"))
		return rackTokens(dark);
	if (resolved == QStringLiteral("matrix"))
		return matrixTokens(dark);
	return studioTokens(dark);
}

QString qssResource(const QString& id, bool dark)
{
	QString resolved = resolveId(id);
	// Historical file names: the minimal skin's sheets keep their original
	// precision_* names on purpose (docs/skins/minimal.md).
	if (resolved == QStringLiteral("minimal"))
		resolved = QStringLiteral("precision");
	return QStringLiteral(":/skins/%1_%2.qss").arg(resolved, dark ? QStringLiteral("dark") : QStringLiteral("light"));
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
		qss.replace(QLatin1String(s.placeholder), s.value);
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
