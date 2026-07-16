/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SkinFileIcons.h"

#include <QFileInfo>
#include <QPainter>
#include <QPixmap>

namespace
{
SkinFileIconProvider::Glyph glyphForSuffix(const QString& suffix)
{
	if (suffix == QLatin1String("wav") || suffix == QLatin1String("flac") || suffix == QLatin1String("ogg"))
		return SkinFileIconProvider::Glyph::AudioFile;
	if (suffix == QLatin1String("txt"))
		return SkinFileIconProvider::Glyph::ConfigFile;
	if (suffix == QLatin1String("dll") || suffix == QLatin1String("vst3"))
		return SkinFileIconProvider::Glyph::PluginFile;
	return SkinFileIconProvider::Glyph::GenericFile;
}
}

void SkinFileIconProvider::updateTokens(const SkinTokens& newTokens)
{
	// The colours a glyph can read are the only cache-relevant inputs; the
	// geometry tokens never change a pictogram.
	const bool changed = tokens.text != newTokens.text
		|| tokens.mutedText != newTokens.mutedText
		|| tokens.accent != newTokens.accent
		|| tokens.accent2 != newTokens.accent2
		|| tokens.warning != newTokens.warning
		|| tokens.card != newTokens.card
		|| tokens.surface != newTokens.surface
		|| tokens.background != newTokens.background
		|| tokens.border != newTokens.border;
	tokens = newTokens;
	if (changed)
		cache.clear();
}

QIcon SkinFileIconProvider::icon(IconType type) const
{
	switch (type)
	{
	case QAbstractFileIconProvider::Folder:
		return cachedIcon(Glyph::Folder);
	case QAbstractFileIconProvider::File:
		return cachedIcon(Glyph::GenericFile);
	case QAbstractFileIconProvider::Drive:
		return cachedIcon(Glyph::Drive);
	case QAbstractFileIconProvider::Computer:
	case QAbstractFileIconProvider::Network:
	case QAbstractFileIconProvider::Desktop:
		return cachedIcon(Glyph::Computer);
	default:
		return QFileIconProvider::icon(type);
	}
}

QIcon SkinFileIconProvider::icon(const QFileInfo& info) const
{
	if (info.isRoot())
		return cachedIcon(Glyph::Drive);
	if (info.isDir())
		return cachedIcon(Glyph::Folder);
	return cachedIcon(glyphForSuffix(info.suffix().toLower()));
}

QIcon SkinFileIconProvider::cachedIcon(Glyph glyph) const
{
	const int key = int(glyph);
	auto it = cache.constFind(key);
	if (it != cache.constEnd())
		return it.value();
	const QIcon icon = makeIcon(glyph, tokens);
	cache.insert(key, icon);
	return icon;
}

QIcon SkinFileIconProvider::paintedIcon(const std::function<void(QPainter&, const QRect&, int)>& paint)
{
	QIcon icon;
	// The small-icon ladder: 16 is what the dialog's views actually request,
	// the larger entries keep 125-200% DPI screens on a crisp per-size render
	// instead of an upscaled 16px bitmap.
	for (const int size : { 16, 20, 24, 32, 48 })
	{
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
		paint(painter, QRect(0, 0, size, size), size);
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}
