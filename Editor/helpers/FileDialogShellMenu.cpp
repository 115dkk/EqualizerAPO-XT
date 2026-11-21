/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FileDialogShellMenu.h"

#include "platform/windows/ShellContextMenu.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QScreen>

#include <string>
#include <vector>

namespace
{
FileDialogShellMenu::Presenter testPresenter;

// Qt reports screen positions in device-independent pixels with each
// screen's origin left in native pixels; the shell wants native pixels.
POINT nativeScreenPoint(const QPoint& globalPos)
{
	QPoint native = globalPos;
	if (const QScreen* screen = QGuiApplication::screenAt(globalPos))
	{
		const QPoint origin = screen->geometry().topLeft();
		native = origin + (QPointF(globalPos - origin) * screen->devicePixelRatio()).toPoint();
	}
	return POINT{ native.x(), native.y() };
}

void presentShellMenu(QFileDialog& dialog, const FileDialogShellMenu::Request& request)
{
	const HWND owner = reinterpret_cast<HWND>(dialog.winId());
	const POINT point = nativeScreenPoint(request.globalPos);
	if (request.items.isEmpty())
	{
		if (!request.folder.isEmpty())
			winutil::showShellFolderBackgroundMenu(owner, request.folder.toStdWString(), point);
		return;
	}
	std::vector<std::wstring> paths;
	paths.reserve(static_cast<size_t>(request.items.size()));
	for (const QString& item : request.items)
		paths.push_back(item.toStdWString());
	winutil::showShellItemMenu(owner, paths, point);
}

FileDialogShellMenu::Request requestAt(const QFileDialog& dialog, const QAbstractItemView& view, const QPoint& viewportPos)
{
	FileDialogShellMenu::Request request;
	request.folder = QDir::toNativeSeparators(dialog.directory().absolutePath());
	request.globalPos = view.viewport()->mapToGlobal(viewportPos);

	const QModelIndex clicked = view.indexAt(viewportPos);
	if (!clicked.isValid())
		return request;

	// The whole selection when the click landed inside it, as in Explorer;
	// otherwise only the item under the pointer.
	QModelIndexList rows;
	if (const QItemSelectionModel* selection = view.selectionModel())
	{
		if (selection->isRowSelected(clicked.row(), clicked.parent()))
			rows = selection->selectedRows(0);
	}
	if (rows.isEmpty())
		rows.append(clicked.siblingAtColumn(0));
	for (const QModelIndex& row : rows)
	{
		const QString path = row.data(QFileSystemModel::FilePathRole).toString();
		if (!path.isEmpty())
			request.items.append(QDir::toNativeSeparators(path));
	}
	return request;
}
}

void FileDialogShellMenu::attach(QFileDialog& dialog)
{
	// QFileDialog's two item views, by Qt's object names. Both already ask
	// for a custom context menu and send it to the dialog's own handler;
	// that connection is replaced, not joined, or two menus would open.
	for (const char* name : { "listView", "treeView" })
	{
		QAbstractItemView* view = dialog.findChild<QAbstractItemView*>(QLatin1String(name));
		if (view == nullptr)
			continue;
		view->setContextMenuPolicy(Qt::CustomContextMenu);
		QObject::disconnect(view, &QWidget::customContextMenuRequested, &dialog, nullptr);
		QObject::connect(view, &QWidget::customContextMenuRequested, &dialog,
			[&dialog, view](const QPoint& viewportPos)
			{
				const Request request = requestAt(dialog, *view, viewportPos);
				if (testPresenter)
					testPresenter(dialog, request);
				else
					presentShellMenu(dialog, request);
			});
	}
}

void FileDialogShellMenu::setPresenterForTest(Presenter presenter)
{
	testPresenter = std::move(presenter);
}
