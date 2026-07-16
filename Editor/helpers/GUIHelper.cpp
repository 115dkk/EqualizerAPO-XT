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

#include <QApplication>
#include <QDir>
#include <QFileDialog>
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

	return QIcon(pixmap);
}

double GUIHelper::knobGainRange()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	const double range = settings.value("interface/knobGainRange", 20.0).toDouble();
	return qBound(1.0, range, 100.0);
}

void GUIHelper::setKnobGainRange(double range)
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue("interface/knobGainRange", qBound(1.0, range, 100.0));
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
	// reads, HKLM ConfigPath), then the user's standard folders. Downloads is
	// included because impulse responses and shared presets usually arrive
	// there.
	QList<QUrl> sidebar;
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
	if (!configRoot.isEmpty() && QDir(configRoot).exists())
		sidebar.append(QUrl::fromLocalFile(QDir(configRoot).absolutePath()));
	for (QStandardPaths::StandardLocation location : { QStandardPaths::DownloadLocation,
			QStandardPaths::DocumentsLocation, QStandardPaths::DesktopLocation, QStandardPaths::HomeLocation })
	{
		const QString path = QStandardPaths::writableLocation(location);
		if (!path.isEmpty())
			sidebar.append(QUrl::fromLocalFile(path));
	}
	dialog.setSidebarUrls(sidebar);

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

	skinManager->styleFileDialog(&dialog);
}
