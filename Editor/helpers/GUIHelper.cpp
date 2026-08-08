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
#include "Editor/helpers/EditorSettings.h"

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
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTreeView>
#include <QUrl>

#include "helpers/RegistryHelper.h"
#include "Editor/SkinManager.h"
#include "Editor/import/LegacyMigration.h"
#include "Editor/widgets/DialogChrome.h"

QSize GUIHelper::scale(QSize size)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return size;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return QSize(qRound(size.width() * dpi / 96), qRound(size.height() * dpi / 96));
}

int GUIHelper::scale(double pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return qRound(pixel);

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return qRound(pixel * dpi / 96);
}

double GUIHelper::scaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * dpi / 96;
}

double GUIHelper::invScale(int pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return pixel;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return pixel * 96 / dpi;
}

double GUIHelper::invScaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * 96 / dpi;
}

bool GUIHelper::isDarkMode()
{
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QIcon GUIHelper::tintedIcon(const QString& resource, const QColor& color, int size)
{
	QIcon base(resource);
	QPixmap pixmap = base.pixmap(scale(QSize(size, size)));
	if (pixmap.isNull())
		return base;

	// Keep the rendered glyph's alpha mask, replace its colour. CompositionMode_SourceIn
	// paints the fill only where the source already has coverage, so the result is the
	// same shape in the requested colour. The fill rect is in device-independent units;
	// any overshoot on a high-DPI pixmap is harmless because untouched pixels stay clear.
	QPainter painter(&pixmap);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(QRect(QPoint(0, 0), pixmap.deviceIndependentSize().toSize()), color);
	painter.end();

	QIcon icon(pixmap);
	// A disabled action must read as disabled from the glyph itself, not only
	// from the button chrome: bake a faded variant instead of relying on the
	// style's generated disabled look, which under the skins left the icon at
	// full strength (undo/redo looked clickable with an empty history).
	icon.addPixmap(fadedPixmap(pixmap), QIcon::Disabled);
	return icon;
}

QPixmap GUIHelper::fadedPixmap(const QPixmap& pixmap)
{
	// DestinationIn multiplies the existing alpha, keeping the glyph's shape
	// and colour while dropping its strength to roughly a third.
	QPixmap faded = pixmap;
	QPainter painter(&faded);
	painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	painter.fillRect(faded.rect(), QColor(0, 0, 0, 90));
	painter.end();
	return faded;
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

void GUIHelper::prepareFileDialog(QFileDialog& dialog)
{
	SkinManager* skinManager = SkinManager::instance();
	if (skinManager->isHeritage())
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

	QString configRoot;
	try
	{
		if (RegistryHelper::keyExists(APP_REGPATH) && RegistryHelper::valueExists(APP_REGPATH, L"ConfigPath"))
			configRoot = QString::fromStdWString(RegistryHelper::readValue(APP_REGPATH, L"ConfigPath"));
	}
	catch (const RegistryException&)
	{
		// Unreadable registry: fall through to the stable root.
	}
	if (configRoot.isEmpty())
		configRoot = EqAPO::Import::LegacyMigration::stableConfigRoot();
	appendSidebar(configRoot);

	// The original Equalizer APO's config folder, for setups that keep the
	// upstream install (or its leftovers) side by side.
	const QString programFiles = QDir::fromNativeSeparators(
		QString::fromLocal8Bit(qgetenv("ProgramFiles")));
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
	dialog.resize(scale(QSize(820, 520)));

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

	// Enough sidebar width for the location labels under the larger skin
	// typefaces; the stock split truncates even short folder names on soft.
	if (QSplitter* splitter = dialog.findChild<QSplitter*>(QStringLiteral("splitter")))
		splitter->setSizes({ scale(150.0), scale(650.0) });

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
