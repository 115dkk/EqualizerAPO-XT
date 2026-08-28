/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The Editor-side vocabulary of every config command, in one table: card
	identity (descriptor type, badge, accent, title), the badge/picker
	pictogram, the picker description, and the add-picker template roster
	with its section grouping and order. Audit #250 B1: this knowledge used
	to live in four tables that did not know about each other (the card
	model's ladder and icon maps, the picker's description table, and the
	sixteen legacy GUI factories' template lists), so adding one filter
	meant touching them all and creating a legacy factory the UI policy
	says is frozen. Consumers now pull from here; the legacy factories keep
	their frozen createFilterGUI role only.

	This unit is QtCore-only on purpose: EditorLogicTests compiles it
	directly, so the roster's completeness (every command has an icon, every
	template parses as its command, the section order is the intended one)
	is asserted as data instead of discovered in the field.

	Translation contexts are pinned per string to wherever the string lived
	before the catalog existed (factory class contexts, "FilterCardModel",
	"FilterPickerView"), so every already-translated string keeps its
	translation. The QT_TRANSLATE_NOOP + QCoreApplication::translate pair is
	the same pattern FilterPickerView's description table already used.
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace FilterCommandCatalog
{
// One row per command keyword the Editor decorates. `keyword` is the
// engine's canonical spelling (FilterFactoryRegistry), plus the Editor-only
// "#" pseudo-command for pure comment lines.
struct CommandEntry
{
	const char* keyword;
	// FilterCardDescriptor type. Shared by design in two places: the
	// Convolution/MultiConvolution siblings both read "convolution" (split
	// by badge) and the whole If family reads "if" (split by badge), so
	// skins style each pair the same way.
	const char* type;
	const char* badge;
	const char* color;
	// Pictogram base name under :/icons/modern/. The Filter keyword's
	// response-curve split lives in biquadCurves() instead; its entry here
	// carries the unparsed-biquad fallback.
	const char* icon;
	bool routeType;
	// Card title, translated in the "FilterCardModel" context.
	const char* title;
	// One-line picker description, translated in the "FilterPickerView"
	// context. nullptr when the command ships without one.
	const char* description;
};

// The biquad response-type vocabulary: pictogram and description per type
// code. Badge matching uses startsWith so the factory's long spellings fold
// onto the eight glyphs (LPQ rides with LP, LSC with LS, PEQ/MODAL fall
// through to the PK default).
struct BiquadCurveEntry
{
	const char* code;
	const char* icon;
	const char* description;
};

// A template's config line is either a literal, or computed at query time:
// the GraphicEQ presets derive from the one ISO band table (audit F023) and
// the SubwooferRouting default state depends on the selected device's
// channel layout. FilterTable resolves the non-literal kinds because the
// inputs (band table, selected device) are its neighbourhood, not data.
enum class TemplateKind
{
	Literal,
	GraphicEQBands15,
	GraphicEQBands31,
	SubwooferRoutingDefaultState
};

struct TemplateEntry
{
	const char* name;
	const char* nameContext;
	const char* line; // nullptr when kind != Literal
	TemplateKind kind;
	const char* section; // nullptr = the unnamed General section (empty path)
	const char* sectionContext;
};

const QList<CommandEntry>& commands();
// Exact, case-sensitive match on the canonical keyword ("Preamp", "#").
const CommandEntry* entryForKeyword(const QString& canonicalKeyword);
// Trimmed, case-insensitive match ("preamp"); also accepts "comment" for
// the "#" row. This is the picker/icon lookup spelling.
const CommandEntry* entryForCommandWord(const QString& word);

const QList<BiquadCurveEntry>& biquadCurves();

// The add-picker roster in its final display order: sections in their
// intended sequence (General, Basic, Parametric, Phase & Time, Graphic
// equalizers, Advanced, Plugins, Speaker management, then Control and
// Branching closing the list), entries in order within each section. The
// order used to be emergent - registry order crossed with a sort-last flag
// and section first-appearance, with the Convolution/MultiConvolution tie
// left to link order - and is now simply this list.
const QList<TemplateEntry>& pickerTemplates();

QString iconResource(const char* baseName);
QString title(const CommandEntry& entry);
QString description(const CommandEntry& entry);
QString curveDescription(const BiquadCurveEntry& entry);
// The one description that cannot key off the type token alone: both
// all-pass templates share " AP ", so the first-order wording is chosen by
// its "Order 1" marker.
QString firstOrderAllPassDescription();
QString templateName(const TemplateEntry& entry);
QStringList templatePath(const TemplateEntry& entry);
}
