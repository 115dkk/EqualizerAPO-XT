/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Shared scaffolding of the skin gallery (Editor/SkinGallery.h) and the
	offscreen gates. The gallery's own renderers live beside this header in
	Editor/gallery/, one file per scene family.
*/

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <QDir>
#include <QList>
#include <QString>
#include <QTemporaryDir>

#include "devices/AbstractAPOInfo.h"

class FilterCardRow;
class ISkin;
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
		unsigned channelCount = 2, unsigned long channelMask = 0x3, const std::wstring& guid = std::wstring(),
		bool receives = false)
		: connection(connection), name(name), input(input), installed(installed),
		channelCount(channelCount), channelMask(channelMask), guid(guid), receives(receives)
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

	// The Send card lists its targets by GUID, so the playback endpoints
	// carry one; the Device rows match by getDeviceString() and ignore it.
	std::wstring getDeviceGuid() const override
	{
		return guid;
	}

	bool receivesFromEndpoints() const override
	{
		return receives;
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
	std::wstring guid;
	bool receives;
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

// What every offscreen gate over gallery-built rows stands on: a scratch
// directory holding the synthetic reference targets (buildReferenceFiles) and
// EAPO_SKIN_GALLERY set, so the reference cards resolve like the gallery's.
// The scratch directory lives as long as the fixture.
class GalleryFixture
{
public:
	// gate names the caller in the failure messages, e.g. "CardMoveTest".
	explicit GalleryFixture(const char* gate);
	GalleryFixture(const GalleryFixture&) = delete;
	GalleryFixture& operator=(const GalleryFixture&) = delete;

	// False when the scratch directory or a reference file could not be
	// written; the constructor has said which, and the gate returns 2.
	bool isValid() const;
	// The synthetic config file the rows resolve their references against.
	const QString& configPath() const;
	// Ends a finished gate with status without unwinding (GallerySupport.cpp).
	[[noreturn]] static void finish(int status);

private:
	QTemporaryDir scratch;
	QString config;
};

// Calls fn for every skin of the roster (Skins::all(), in SkinThemeData's
// order), dark first and then light: the walk the gates make.
void forEachSkinMode(const std::function<void(ISkin* skin, bool dark)>& fn);

// The rack engraved label's width contract: given the width its size hint
// asked for, an elidable label prints its whole text, and squeezed below it,
// it still elides. Returns the number of failing texts (GallerySelfTests.cpp).
int checkEngravedLabelFit();
}
