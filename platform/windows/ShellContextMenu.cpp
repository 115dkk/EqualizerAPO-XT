/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2026  115dkk

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#include "ShellContextMenu.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <limits>
#include <string_view>

#include "platform/windows/ComPtr.h"

namespace winutil
{
namespace
{
constexpr UINT idFirst = 1;
constexpr UINT idLast = 0x7fff;

// Preserve the SDK's PIDL pointer type, including __unaligned in non-strict
// mode. Shell headers can already have been included by the Qt precompiled header.
class Pidl
{
public:
	Pidl() = default;
	~Pidl() { ILFree(value); }
	Pidl(const Pidl&) = delete;
	Pidl& operator=(const Pidl&) = delete;

	PIDLIST_ABSOLUTE get() const { return value; }
	explicit operator bool() const { return value != nullptr; }
	PIDLIST_ABSOLUTE* put()
	{
		ILFree(value);
		value = nullptr;
		return &value;
	}

private:
	PIDLIST_ABSOLUTE value = nullptr;
};

bool isAbsolutePath(std::wstring_view path)
{
	if (path.empty() || path.find(L'\0') != std::wstring_view::npos)
		return false;

	// Accept drive-rooted and UNC paths, including their extended forms.
	// A rooted path without a drive, or C:filename, still depends on the CWD.
	if (path.starts_with(L"\\\\?\\"))
	{
		path.remove_prefix(4);
		if (path.starts_with(L"UNC\\"))
			path.remove_prefix(3);
		else
			return path.size() >= 3 &&
				((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
				path[1] == L':' && path[2] == L'\\';
	}
	else if (path.size() >= 3 &&
		((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
		path[1] == L':' && path[2] == L'\\')
	{
		return true;
	}
	else
	{
		if (!path.starts_with(L"\\\\") || path.starts_with(L"\\\\.\\"))
			return false;
		path.remove_prefix(1);
	}

	const size_t serverEnd = path.find(L'\\', 1);
	return serverEnd != std::wstring_view::npos && serverEnd > 1 &&
		serverEnd + 1 < path.size() && path[serverEnd + 1] != L'\\';
}

bool parsePath(const std::wstring& path, SFGAOF required, Pidl& pidl)
{
	std::wstring nativePath = path;
	std::replace(nativePath.begin(), nativePath.end(), L'/', L'\\');
	if (!isAbsolutePath(nativePath))
		return false;

	SFGAOF attributes = required;
	return SUCCEEDED(SHParseDisplayName(nativePath.c_str(), nullptr, pidl.put(), required, &attributes)) &&
		pidl && (attributes & required) == required;
}

bool itemContextMenu(HWND owner, const std::vector<std::wstring>& paths, ComPtr<IContextMenu>& contextMenu)
{
	if (paths.empty() || paths.size() > (std::numeric_limits<UINT>::max)())
		return false;

	// The child pointers returned by SHBindToParent borrow these absolute PIDLs.
	std::vector<Pidl> pidls(paths.size());
	std::vector<PCUITEMID_CHILD> children(paths.size());
	Pidl firstParent;
	ComPtr<IShellFolder> parent;
	ComPtr<IShellFolder> desktop;
	if (FAILED(SHGetDesktopFolder(desktop.put())) || !desktop)
		return false;

	for (size_t index = 0; index < paths.size(); ++index)
	{
		if (!parsePath(paths[index], SFGAO_FILESYSTEM, pidls[index]))
			return false;

		Pidl parentPidl;
		*parentPidl.put() = ILCloneFull(pidls[index].get());
		if (!parentPidl || !ILRemoveLastID(parentPidl.get()))
			return false;

		if (index == 0)
		{
			*firstParent.put() = ILCloneFull(parentPidl.get());
			if (!firstParent)
				return false;
		}
		else
		{
			// Compare identities, not the spelling of paths or PIDL bytes.
			const HRESULT comparison = desktop->CompareIDs(SHCIDS_CANONICALONLY, firstParent.get(), parentPidl.get());
			if (FAILED(comparison) || HRESULT_CODE(comparison) != 0)
				return false;
		}

		ComPtr<IShellFolder> itemParent;
		if (FAILED(SHBindToParent(pidls[index].get(), IID_IShellFolder,
			reinterpret_cast<void**>(itemParent.put()), &children[index])) || !itemParent || !children[index])
			return false;
		if (index == 0)
			parent = std::move(itemParent);
	}

	return SUCCEEDED(parent->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(),
		IID_IContextMenu, nullptr, reinterpret_cast<void**>(contextMenu.put()))) && contextMenu;
}

bool backgroundContextMenu(HWND owner, const std::wstring& path, ComPtr<IContextMenu>& contextMenu)
{
	Pidl pidl;
	if (!parsePath(path, SFGAO_FILESYSTEM | SFGAO_FOLDER, pidl))
		return false;

	ComPtr<IShellFolder> desktop;
	ComPtr<IShellFolder> folder;
	if (FAILED(SHGetDesktopFolder(desktop.put())) || !desktop ||
		FAILED(desktop->BindToObject(pidl.get(), nullptr, IID_IShellFolder,
			reinterpret_cast<void**>(folder.put()))) || !folder)
		return false;

	return SUCCEEDED(folder->CreateViewObject(owner, IID_IContextMenu,
		reinterpret_cast<void**>(contextMenu.put()))) && contextMenu;
}

class PopupMenu
{
public:
	PopupMenu() : menu(CreatePopupMenu()) {}
	~PopupMenu()
	{
		if (menu)
			DestroyMenu(menu);
	}

	PopupMenu(const PopupMenu&) = delete;
	PopupMenu& operator=(const PopupMenu&) = delete;
	HMENU get() const { return menu; }

private:
	HMENU menu;
};

bool isRename(IContextMenu* contextMenu, UINT command, UINT commandCount)
{
	if (command < idFirst || command > idLast || command - idFirst >= commandCount)
		return false;

	wchar_t verb[256] = {};
	const HRESULT hr = contextMenu->GetCommandString(command - idFirst, GCS_VERBW, nullptr,
		reinterpret_cast<char*>(verb), static_cast<UINT>(std::size(verb)));
	verb[std::size(verb) - 1] = L'\0';
	return SUCCEEDED(hr) && CompareStringOrdinal(verb, -1, L"rename", -1, TRUE) == CSTR_EQUAL;
}

bool inspectRenames(IContextMenu* contextMenu, HMENU menu, UINT commandCount,
	bool remove, bool& found, unsigned depth = 0)
{
	const int count = GetMenuItemCount(menu);
	if (count < 0 || depth > 64)
		return false;

	// Work backwards so deleting an item does not change unvisited positions.
	for (int index = count - 1; index >= 0; --index)
	{
		MENUITEMINFOW item = {};
		item.cbSize = sizeof(item);
		item.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
		if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &item))
			return false;
		if (!(item.fType & MFT_SEPARATOR) && isRename(contextMenu, item.wID, commandCount))
		{
			found = true;
			if (remove)
			{
				if (!DeleteMenu(menu, static_cast<UINT>(index), MF_BYPOSITION))
					return false;
				continue;
			}
		}
		if (item.hSubMenu && !inspectRenames(contextMenu, item.hSubMenu, commandCount, remove, found, depth + 1))
			return false;
	}
	return true;
}

bool populateMenu(IContextMenu* contextMenu, HMENU menu, UINT& commandCount)
{
	if (!menu)
		return false;
	UINT flags = CMF_NORMAL;
	if (GetKeyState(VK_SHIFT) < 0)
		flags |= CMF_EXTENDEDVERBS;
	// There is no in-place editor: never advertise CMF_CANRENAME.
	const HRESULT hr = contextMenu->QueryContextMenu(menu, 0, idFirst, idLast, flags);
	if (FAILED(hr))
		return false;
	commandCount = HRESULT_CODE(hr);
	if (commandCount > idLast - idFirst + 1)
		return false;
	bool found = false;
	return inspectRenames(contextMenu, menu, commandCount, true, found) && GetMenuItemCount(menu) > 0;
}

class MenuSubclass
{
public:
	MenuSubclass(HWND window, IContextMenu* menu, UINT count)
		: contextMenu(menu), commandCount(count)
	{
		if (FAILED(menu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(contextMenu3.put()))) || !contextMenu3)
			menu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(contextMenu2.put()));
		if (SetWindowSubclass(window, windowProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)))
			owner = window;
	}

	~MenuSubclass()
	{
		if (owner)
			RemoveWindowSubclass(owner, windowProc, reinterpret_cast<UINT_PTR>(this));
	}

	MenuSubclass(const MenuSubclass&) = delete;
	MenuSubclass& operator=(const MenuSubclass&) = delete;
	bool installed() const { return owner != nullptr; }
	bool failed() const { return messageFailed; }
	bool shown() const { return menuShown; }

private:
	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
		UINT_PTR subclassId, DWORD_PTR reference)
	{
		auto* self = reinterpret_cast<MenuSubclass*>(reference);
		if (message == WM_NCDESTROY)
		{
			RemoveWindowSubclass(window, windowProc, subclassId);
			self->owner = nullptr;
			return DefSubclassProc(window, message, wParam, lParam);
		}

		const bool menuMessage = message == WM_INITMENUPOPUP || message == WM_MENUCHAR ||
			((message == WM_DRAWITEM || message == WM_MEASUREITEM) && wParam == 0);
		if (menuMessage)
		{
			try
			{
				LRESULT result = 0;
				bool handled = false;
				if (self->contextMenu3)
					handled = self->contextMenu3->HandleMenuMsg2(message, wParam, lParam, &result) == S_OK;
				else if (self->contextMenu2 && message != WM_MENUCHAR)
				{
					handled = self->contextMenu2->HandleMenuMsg(message, wParam, lParam) == S_OK;
					result = message == WM_INITMENUPOPUP ? 0 : TRUE;
				}

				// Lazy submenus can add verbs after the initial QueryContextMenu.
				if (message == WM_INITMENUPOPUP)
				{
					self->menuShown = true;
					bool found = false;
					if (!inspectRenames(self->contextMenu, reinterpret_cast<HMENU>(wParam),
						self->commandCount, true, found))
					{
						self->messageFailed = true;
						EndMenu();
						return 0;
					}
				}
				if (handled)
					return result;
			}
			catch (...)
			{
				// Never unwind through a Win32 callback, including a shell extension.
				self->messageFailed = true;
				EndMenu();
				return 0;
			}
		}
		return DefSubclassProc(window, message, wParam, lParam);
	}

	HWND owner = nullptr;
	IContextMenu* contextMenu;
	UINT commandCount;
	ComPtr<IContextMenu3> contextMenu3;
	ComPtr<IContextMenu2> contextMenu2;
	bool messageFailed = false;
	bool menuShown = false;
};

bool validOwner(HWND owner)
{
	return owner && GetWindowThreadProcessId(owner, nullptr) == GetCurrentThreadId();
}

ShellMenuResult trackMenu(HWND owner, IContextMenu* contextMenu, POINT screenPoint)
{
	PopupMenu menu;
	UINT commandCount = 0;
	if (!populateMenu(contextMenu, menu.get(), commandCount))
		return ShellMenuResult::Unavailable;

	UINT command = 0;
	{
		MenuSubclass subclass(owner, contextMenu, commandCount);
		if (!subclass.installed())
			return ShellMenuResult::Unavailable;
		const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
		command = static_cast<UINT>(TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | alignment,
			screenPoint.x, screenPoint.y, owner, nullptr));
		// The menu loop above runs the subclass procedure, which uninstalls
		// itself when the owner is destroyed while the menu is open, so this
		// is not the check from before the loop repeated.
		// cppcheck-suppress identicalConditionAfterEarlyExit
		if (subclass.failed() || !subclass.installed() || !subclass.shown())
			return ShellMenuResult::Unavailable;
	}
	// The active dialog already owns the foreground; no notification-icon
	// foreground activation or WM_NULL workaround belongs here.
	if (command == 0)
		return ShellMenuResult::Dismissed;
	if (command < idFirst || command > idLast || command - idFirst >= commandCount ||
		isRename(contextMenu, command, commandCount) || !validOwner(owner))
		return ShellMenuResult::Unavailable;

	CMINVOKECOMMANDINFOEX invoke = {};
	invoke.cbSize = sizeof(invoke);
	invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
	if (GetKeyState(VK_SHIFT) < 0)
		invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
	if (GetKeyState(VK_CONTROL) < 0)
		invoke.fMask |= CMIC_MASK_CONTROL_DOWN;
	invoke.hwnd = owner;
	invoke.lpVerb = MAKEINTRESOURCEA(command - idFirst);
	invoke.lpVerbW = MAKEINTRESOURCEW(command - idFirst);
	invoke.nShow = SW_SHOWNORMAL;
	invoke.ptInvoke = screenPoint;
	return SUCCEEDED(contextMenu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invoke)))
		? ShellMenuResult::Invoked : ShellMenuResult::Unavailable;
}

bool inspectMenuForTest(IContextMenu* contextMenu, int& itemCount, bool& hasRenameVerb)
{
	PopupMenu menu;
	UINT commandCount = 0;
	if (!populateMenu(contextMenu, menu.get(), commandCount) ||
		!inspectRenames(contextMenu, menu.get(), commandCount, false, hasRenameVerb))
		return false;
	itemCount = GetMenuItemCount(menu.get());
	return itemCount > 0;
}
}

ShellMenuResult showShellItemMenu(HWND owner, const std::vector<std::wstring>& paths, POINT screenPoint)
{
	try
	{
		if (!validOwner(owner))
			return ShellMenuResult::Unavailable;
		ComPtr<IContextMenu> contextMenu;
		if (!itemContextMenu(owner, paths, contextMenu))
			return ShellMenuResult::Unavailable;
		return trackMenu(owner, contextMenu.get(), screenPoint);
	}
	catch (...)
	{
		return ShellMenuResult::Unavailable;
	}
}

ShellMenuResult showShellFolderBackgroundMenu(HWND owner, const std::wstring& folder, POINT screenPoint)
{
	try
	{
		if (!validOwner(owner))
			return ShellMenuResult::Unavailable;
		ComPtr<IContextMenu> contextMenu;
		if (!backgroundContextMenu(owner, folder, contextMenu))
			return ShellMenuResult::Unavailable;
		return trackMenu(owner, contextMenu.get(), screenPoint);
	}
	catch (...)
	{
		return ShellMenuResult::Unavailable;
	}
}

namespace detail
{
bool buildShellItemMenuForTest(const std::vector<std::wstring>& paths, int& itemCount, bool& hasRenameVerb)
{
	itemCount = 0;
	hasRenameVerb = false;
	try
	{
		ComPtr<IContextMenu> contextMenu;
		return itemContextMenu(nullptr, paths, contextMenu) && inspectMenuForTest(contextMenu.get(), itemCount, hasRenameVerb);
	}
	catch (...)
	{
		return false;
	}
}

bool buildShellFolderBackgroundMenuForTest(const std::wstring& folder, int& itemCount, bool& hasRenameVerb)
{
	itemCount = 0;
	hasRenameVerb = false;
	try
	{
		ComPtr<IContextMenu> contextMenu;
		return backgroundContextMenu(nullptr, folder, contextMenu) && inspectMenuForTest(contextMenu.get(), itemCount, hasRenameVerb);
	}
	catch (...)
	{
		return false;
	}
}
}
}
