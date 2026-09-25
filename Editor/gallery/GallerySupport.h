/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Shared scaffolding of the skin gallery (Editor/SkinGallery.h) and the
	offscreen gates. The gallery's own renderers live beside this header in
	Editor/gallery/, one file per scene family.
*/

#pragma once

#include <memory>
#include <string>

#include <QDir>
#include <QList>
#include <QString>

#include "devices/AbstractAPOInfo.h"

class FilterCardRow;
class QScrollArea;
class QToolBar;
class QWidget;

namespace SkinGalleryDetail
{
struct GalleryRow
{
	QString name;
	QString line;
};

// The representative rows every skin is judged on (see GallerySupport.cpp).
QList<GalleryRow> galleryRows();

// Synthetic audio endpoints for the Device rows. Offscreen runners have no
// audio devices and the Device card only grows chips for enumerated
// endpoints, so without these the card renders as a lone master chip and the
// per-skin switch grammar is never judged. Three playback endpoints (one
// engaged, one idle, one without the APO - the blank hidden behind the
// reveal toggle) and one engaged capture endpoint cover the state family.
class GalleryAPOInfo : public AbstractAPOInfo
{
public:
	GalleryAPOInfo(const std::wstring& connection, const std::wstring& name, bool input, bool installed,
		unsigned channelCount = 2, unsigned long channelMask = 0x3)
		: connection(connection), name(name), input(input), installed(installed),
		channelCount(channelCount), channelMask(channelMask)
	{
	}

	std::wstring getConnectionName() const override
	{
		return connection;
	}

	std::wstring getDeviceName() const override
	{
		return name;
	}

	std::wstring getDeviceGuid() const override
	{
		return L"";
	}

	// The card pre-selects a chip when the row's pattern matches this string
	// (DeviceCommand::matches) and serializes selections back as these exact
	// strings joined with "; " - keep them plain words so the gallery line
	// round-trips byte-identically.
	std::wstring getDeviceString() const override
	{
		return connection + L" " + name;
	}

	unsigned getChannelCount() const override
	{
		return channelCount;
	}

	unsigned getSampleRate() const override
	{
		return 48000;
	}

	unsigned long getChannelMask() const override
	{
		return channelMask;
	}

	bool isInput() const override
	{
		return input;
	}

	bool isInstalled() const override
	{
		return installed;
	}

	bool canBeUpgraded() const override
	{
		return false;
	}

	bool hasChanges() const override
	{
		return false;
	}

	bool isEnhancementsDisabled() const override
	{
		return false;
	}

	bool isDefaultDevice() const override
	{
		return false;
	}

	bool isDisabled() const override
	{
		return false;
	}

	bool isUnplugged() const override
	{
		return false;
	}

	void install() override
	{
	}

	void uninstall() override
	{
	}

	void reinstall() override
	{
	}

private:
	std::wstring connection;
	std::wstring name;
	bool input;
	bool installed;
	unsigned channelCount;
	unsigned long channelMask;
};

void galleryDevices(QList<std::shared_ptr<AbstractAPOInfo>>& outputs, QList<std::shared_ptr<AbstractAPOInfo>>& inputs);
// Synthetic reference targets next to a synthetic config file; returns the
// config path, or an empty string on failure.
QString buildReferenceFiles(const QDir& outDir);
// The fixed fixture folder under %TEMP%, independent of the output directory.
QDir galleryFixtureRoot();
QString buildFileDialogFixture(const QDir& fixtureRoot);
// Faithful chrome replica of MainWindow's toolbar.
QToolBar* buildToolbarReplica(QWidget* parent);
// A fresh FilterTable holding the given lines, hosted like MainWindow hosts
// it; returns the card rows in line order.
QList<FilterCardRow*> buildRows(QScrollArea& scrollArea, const QString& configPath, const QList<QString>& lines,
	std::shared_ptr<AbstractAPOInfo> device = nullptr, unsigned long channelMask = 0);
}
