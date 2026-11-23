/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2016  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "GUIHelper.h"
#include "services/registry/RegistryPaths.h"
#include "services/settings/EditorSettings.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QSet>
#include <QFont>
#include <QFontMetrics>
#include <QHeaderView>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyleHints>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>

#include "services/registry/WindowsRegistry.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/FileDialogShellMenu.h"
#include "Editor/import/LegacyMigration.h"
#include "Editor/widgets/DialogChrome.h"
#include "platform/qt/TintedIcon.h"

bool GUIHelper::isDarkMode()
{
	// The graph rulers, the GraphicEQ nodes and the legacy row frames used to
	// read the system scheme here, so a light skin on a dark Windows drew
	// white rulers on a light graph.
	return SkinManager::instance()->isDark();
}

bool GUIHelper::isSystemDarkMode()
{
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// Both live in platform/qt/TintedIcon, which the Device Selector compiles too.
QIcon GUIHelper::tintedIcon(const QString& resource, const QColor& color, int size)
{
	return TintedIcon::make(resource, color, size);
}

QPixmap GUIHelper::fadedPixmap(const QPixmap& pixmap)
{
	return TintedIcon::faded(pixmap);
}

double GUIHelper::knobGainRange()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	const double range = settings.value(QLatin1String(EditorSettings::Keys::KnobGainRange), 20.0).toDouble();
	return qBound(1.0, range, 100.0);
}

void GUIHelper::setKnobGainRange(double range)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue(QLatin1String(EditorSettings::Keys::KnobGainRange), qBound(1.0, range, 100.0));
}

bool GUIHelper::useNativeFileDialog()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	return settings.value(QLatin1String(EditorSettings::Keys::NativeFileDialog), false).toBool();
}

void GUIHelper::setUseNativeFileDialog(bool native)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue(QLatin1String(EditorSettings::Keys::NativeFileDialog), native);
}

void GUIHelper::prepareFileDialog(QFileDialog& dialog)
{
	SkinManager* skinManager = SkinManager::instance();
	if (skinManager->isHeritage() || useNativeFileDialog())
		return;

	// The widget-based dialog inherits the app-wide skin sheet; setting the
	// option also makes QFileDialog build its widget tree now, so the skin
	// hook below can reach the navigation buttons.
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	dialog.setViewMode(QFileDialog::Detail);

	// Sidebar: the active config root first (the folder the engine actually
	// reads, HKLM ConfigPath), the upstream Equalizer APO's config folder,
	// the folders of recently opened configurations, the user's standard
	// folders (Downloads because impulse responses and shared presets
	// usually arrive there), and the drive roots Explorer-style. The file
	// system model names drives by the Windows convention ("Local Disk
	// (C:)"), and the skin's icon provider carries a dedicated drive glyph.
	QList<QUrl> sidebar;
	QSet<QString> sidebarSeen;
	auto appendSidebar = [&sidebar, &sidebarSeen](const QString& path)
	{
		if (path.isEmpty())
			return false;
		const QFileInfo info(path);
		if (!info.isDir())
			return false;
		const QString canonical = QDir::cleanPath(info.absoluteFilePath());
		const QString key = canonical.toLower();
		if (sidebarSeen.contains(key))
			return false;
		sidebarSeen.insert(key);
		sidebar.append(QUrl::fromLocalFile(canonical));
		return true;
	};

	appendSidebar(EqAPO::Import::LegacyMigration::configRoot(systemRegistry()));

	// The original Equalizer APO's config folder, for setups that keep the
	// upstream install (or its leftovers) side by side.
	const QString programFiles = QDir::fromNativeSeparators(
		qEnvironmentVariable("ProgramFiles"));
	if (!programFiles.isEmpty())
		appendSidebar(programFiles + QStringLiteral("/EqualizerAPO/config"));

	// The folders of recently opened configurations (the File menu's list).
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		const QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
		int added = 0;
		for (const QString& file : recent)
		{
			if (added >= 4)
				break;
			if (appendSidebar(QFileInfo(file).absolutePath()))
				added++;
		}
	}

	for (QStandardPaths::StandardLocation location : { QStandardPaths::DownloadLocation,
			QStandardPaths::DocumentsLocation, QStandardPaths::DesktopLocation, QStandardPaths::HomeLocation })
		appendSidebar(QStandardPaths::writableLocation(location));

	for (const QFileInfo& drive : QDir::drives())
		appendSidebar(drive.absoluteFilePath());

	dialog.setSidebarUrls(sidebar);

	// The location dropdown accepts a pasted path: Explorer users copy a
	// location - often via "Copy as path", quotes included - and expect to
	// land on it directly instead of clicking down the tree. The dropdown
	// itself (history + places popup) stays.
	if (QComboBox* lookInCombo = dialog.findChild<QComboBox*>(QStringLiteral("lookInCombo")))
	{
		lookInCombo->setEditable(true);
		lookInCombo->setInsertPolicy(QComboBox::NoInsert);
		if (QLineEdit* pathEdit = lookInCombo->lineEdit())
		{
			QObject::connect(pathEdit, &QLineEdit::returnPressed, &dialog,
				[&dialog, pathEdit]()
				{
					QString typed = pathEdit->text().trimmed();
					if (typed.size() >= 2 && typed.startsWith(QLatin1Char('"'))
						&& typed.endsWith(QLatin1Char('"')))
						typed = typed.mid(1, typed.size() - 2);
					if (typed.isEmpty())
						return;
					const QFileInfo info(QDir::fromNativeSeparators(typed));
					if (info.isFile())
					{
						// A full file path selects the file itself.
						dialog.setDirectory(info.absolutePath());
						dialog.selectFile(info.absoluteFilePath());
					}
					else if (info.isDir())
					{
						dialog.setDirectory(info.absoluteFilePath());
					}
				});
		}
	}

	// A roomier default than QFileDialog's compact size hint, so the Detail
	// columns (name/size/date) fit without immediate scrolling.
	dialog.resize(QSize(820, 520));

	// Readable Detail columns: the name column takes the free width and the
	// metadata columns track their content. The stock dialog leaves every
	// column at a narrow default that truncates even short cells.
	if (QTreeView* view = dialog.findChild<QTreeView*>(QStringLiteral("treeView")))
	{
		QHeaderView* header = view->header();
		header->setSectionResizeMode(0, QHeaderView::Stretch);
		for (int section = 1; section < header->count(); section++)
			header->setSectionResizeMode(section, QHeaderView::ResizeToContents);
	}

	// No "new folder" button: it sat right next to "parent folder" and was
	// hit by mistake far more often than on purpose (issue #508). Making a
	// folder is still there, in the folder background's shell menu below.
	if (QToolButton* newFolder = dialog.findChild<QToolButton*>(QStringLiteral("newFolderButton")))
		newFolder->hide();

	// Right-click opens Explorer's own menu instead of Qt's three-entry one,
	// so shell extensions (search tools, archivers, Properties) work here as
	// they do in the Windows dialog.
	FileDialogShellMenu::attach(dialog);

	// Enough sidebar width for the location labels under the larger skin
	// typefaces; the stock split truncates even short folder names on flat.
	if (QSplitter* splitter = dialog.findChild<QSplitter*>(QStringLiteral("splitter")))
		splitter->setSizes({ 150, 650 });

	// The same skinned caption the main window wears - title text plus the
	// conventional close X - replaces the native Windows caption, which was
	// the one remaining piece of stock chrome on a skinned dialog. The
	// registry escape hatch that restores the native caption on the main
	// window applies here too.
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	if (!settings.value(QLatin1String(EditorSettings::Keys::NativeTitleBar), false).toBool())
		DialogChrome::attach(&dialog);

	skinManager->styleFileDialog(&dialog);
}

void GUIHelper::enableVst3BundleSelection(QFileDialog& dialog)
{
	// directoryEntered only fires reliably from the widget-based dialog, so
	// the native dialog is opted out here even where prepareFileDialog kept
	// it - see the header comment.
	if (useNativeFileDialog())
		return;
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	QObject::connect(&dialog, &QFileDialog::directoryEntered, &dialog,
		[&dialog](const QString& path)
		{
			if (!path.endsWith(QStringLiteral(".vst3"), Qt::CaseInsensitive))
				return;
			dialog.selectFile(path);
			// Queued: accepting synchronously would tear the dialog down in
			// the middle of its own navigation signal.
			QMetaObject::invokeMethod(&dialog, "accept", Qt::QueuedConnection);
		});
}

QString GUIHelper::vst3BundleForInnerBinary(const QString& path)
{
	const QFileInfo binary(path);
	if (binary.suffix().compare(QLatin1String("vst3"), Qt::CaseInsensitive) != 0 || !binary.isFile())
		return path;
	QDir platformDir = binary.absoluteDir();
	QDir contentsDir = platformDir;
	if (!contentsDir.cdUp() || contentsDir.dirName().compare(QLatin1String("Contents"), Qt::CaseInsensitive) != 0)
		return path;
	QDir bundleDir = contentsDir;
	if (!bundleDir.cdUp() || !bundleDir.dirName().endsWith(QLatin1String(".vst3"), Qt::CaseInsensitive))
		return path;
	return bundleDir.absolutePath();
}
