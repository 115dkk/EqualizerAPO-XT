/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The data half of the skin system: id aliases, per-skin colour/metric token
	tables, QSS resource paths, the @TOKEN@ substitution and the token-derived
	widget palette. Everything here is behaviour-free (no pickers, renderers or
	chrome painters), so satellite executables - first user: DeviceSelector -
	can compile this single unit plus the .qss resources and wear the exact
	skin the user picked in the Editor, without linking the Editor's widget
	stack. The full ISkin classes delegate their tokens()/qssResource() here,
	so the tables cannot drift apart.
*/

#pragma once

#include <QPalette>
#include <QString>

#include "Editor/SkinTokens.h"

class QApplication;

namespace SkinThemeData
{
// Registers the shared static font faces and fallback chain. Editor passes
// includeSarasa=true for its monospace CJK surfaces; satellite tools keep the
// smaller common set.
void registerBundledFonts(bool includeSarasa = false);

// Applies the complete process theme contract: optional Fusion base style,
// token palette, QSS with Studio fallback, and common widget overrides.
void applyToApplication(QApplication& app, const QString& skinId, bool dark,
	bool setFusionStyle = true, bool includeSarasa = false);

// Canonical skin id for any stored value: applies the legacy aliases
// (glassy -> studio, industrial -> rack) and falls back to "studio" for
// unknown ids, mirroring Skins::byId.
QString resolveId(const QString& id);

// The token table for a (resolved or unresolved) skin id.
SkinTokens tokens(const QString& id, bool dark);

// The ":/skins/..." QSS resource path for the id, honouring the historical
// precision_* file names of the minimal skin.
QString qssResource(const QString& id, bool dark);

// Replaces the @TOKEN@ sentinels of a skin sheet with the token values.
QString substituteTokens(QString qss, const SkinTokens& tokens);

// Token-derived QPalette for the widgets QSS does not cover (item views,
// native popups). The same mapping SkinManager::applySkin applies in the
// Editor on every skin/dark switch.
QPalette palette(const SkinTokens& tokens, bool dark);

// App-wide combo/spin arrow override appended AFTER a skin sheet: the CSS
// border-triangle trick every sheet uses collapses to a dash on Qt 6.10, so
// the arrows are replaced with a real chevron SVG. Needs the
// :/icons/modern/chevron-*.svg resources.
QString comboArrowOverride();

// App-wide file-dialog override appended AFTER a skin sheet, like the combo
// arrows: the non-native QFileDialog's navigation buttons are icon-only, so
// every sheet's text-button QToolButton padding (up to 5px 12px on soft)
// squeezes the icon out of its content box. Scoped to QFileDialog so the
// skins' regular tool buttons keep their padding language.
QString fileDialogOverride();
}
