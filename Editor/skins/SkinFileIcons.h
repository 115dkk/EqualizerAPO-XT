/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Shared base for the skins' file-dialog icon providers. The non-native
	QFileDialog asks its icon provider for every folder/file pictogram (list,
	sidebar, look-in combo), which is the last place the platform shell's
	imagery survives inside a skinned session. The base owns the shared
	semantics - classifying an entry into a small glyph vocabulary and
	caching the rendered icons - while each skin answers the rendering with
	its own philosophy (differentiation gate, docs/skins/README.md).
	Interaction stays with QFileDialog; a provider only paints.
*/

#pragma once

#include <functional>

#include <QFileIconProvider>
#include <QHash>
#include <QIcon>

#include "Editor/SkinTokens.h"

class SkinFileIconProvider : public QFileIconProvider
{
public:
	// The shared glyph vocabulary: what a dialog entry *is*. Folder, the
	// engine's own file families (configurations, impulse responses, VST
	// libraries), the generic rest, and the two shell places the look-in
	// combo shows (drives and the computer root).
	enum class Glyph
	{
		Folder,
		ConfigFile,
		AudioFile,
		PluginFile,
		GenericFile,
		Drive,
		Computer
	};

	// Refresh the palette the glyphs are rendered with. Clears the icon
	// cache when the tokens actually changed, so a dark/light or skin
	// switch cannot serve stale colours from a long-lived provider.
	void updateTokens(const SkinTokens& tokens);

	QIcon icon(IconType type) const override;
	QIcon icon(const QFileInfo& info) const override;

protected:
	// The skin's rendering of one glyph in the current palette.
	virtual QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const = 0;

	// Build a multi-size QIcon from a paint callback. The callback paints
	// one glyph into rect (a square of the given edge length); it runs once
	// per entry of the small-icon ladder so HiDPI views pick a crisply
	// rendered size instead of scaling one bitmap.
	static QIcon paintedIcon(const std::function<void(QPainter&, const QRect&, int)>& paint);

private:
	QIcon cachedIcon(Glyph glyph) const;

	SkinTokens tokens;
	mutable QHash<int, QIcon> cache;
};
