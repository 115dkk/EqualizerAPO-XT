/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "HeritageSkin.h"

#include "Skins.h"

QString HeritageSkin::id() const
{
	return QStringLiteral("heritage");
}

SkinTokens HeritageSkin::tokens(bool dark) const
{
	Q_UNUSED(dark);
	// Classic light values for the custom painters that consume tokens. The
	// widget chrome itself comes from the native style, untouched by QSS.
	// Studio donates the values the block below does not overwrite (metrics,
	// secondary hues), exactly as SkinManager::applyHeritage always did.
	SkinTokens tokens = Skins::byId(QStringLiteral("studio"))->tokens(false);
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

QString HeritageSkin::qssResource(bool dark) const
{
	Q_UNUSED(dark);
	return QString();
}

IRoutingRenderer* HeritageSkin::routingRenderer() const
{
	return nullptr;
}

void HeritageSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	// Native toolbar: the .ui's classic icons stay in place.
	Q_UNUSED(toolBar);
	Q_UNUSED(tokens);
}

void HeritageSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	// The dialog stays platform-native in heritage mode.
	Q_UNUSED(dialog);
	Q_UNUSED(tokens);
}

ISkin* heritageSkin()
{
	static HeritageSkin instance;
	return &instance;
}
