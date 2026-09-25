/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include "platform/windows/Win32Resource.h"
#include <winioctl.h>
#include <winternl.h>
#include <limits>

#include "ConfigPathPolicy.h"

using std::vector;
using std::wstring;

namespace
{
using Entry = ConfigPathPolicy::Entry;

bool isSeparator(wchar_t character)
{
	return character == L'\\' || character == L'/';
}

wstring lowered(wstring text)
{
	for (wchar_t& character : text)
		character = static_cast<wchar_t>(std::towlower(character));
	return text;
}

// \??\ is the NT spelling of \\?\. CreateFileW hands a path that starts with
// it to the kernel unchanged (std::filesystem keeps it absolute on the way
// there), so it names the same places; a link's stored target is spelled
// this way too.
wstring withoutNtPrefix(const wstring& path)
{
	if (path.size() >= 4 && path[0] == L'\\' && path[1] == L'?' && path[2] == L'?' && path[3] == L'\\')
		return L"\\\\?\\" + path.substr(4);
	return path;
}

bool isVerbatim(const wstring& path)
{
	return path.size() >= 4 && path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\';
}

// The components of path from index on, split at either separator.
vector<wstring> componentsFrom(const wstring& path, size_t index)
{
	vector<wstring> components;
	while (index < path.size())
	{
		while (index < path.size() && isSeparator(path[index]))
			++index;
		const size_t begin = index;
		while (index < path.size() && !isSeparator(path[index]))
			++index;
		if (index > begin)
			components.push_back(path.substr(begin, index - begin));
	}
	return components;
}

bool isDriveSpec(const wstring& component)
{
	return component.size() == 2 && std::iswalpha(component[0]) && component[1] == L':';
}

bool isVolumeName(const wstring& component)
{
	return lowered(component).rfind(L"volume{", 0) == 0;
}

// The `?` of \\?\ and the `.` of \\.\.
bool isLocalDevicePrefix(const wstring& component)
{
	return component == L"?" || component == L".";
}

// A local path split where the walk starts: the root it cannot climb above
// and the components below it.
struct LocalPath
{
	wstring root;
	wchar_t driveLetter = 0;
	vector<wstring> components;
};

bool splitLocal(const wstring& path, LocalPath& out)
{
	if (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && isSeparator(path[2]))
	{
		out.root = path.substr(0, 2) + L"\\";
		out.driveLetter = static_cast<wchar_t>(std::towupper(path[0]));
		out.components = componentsFrom(path, 2);
		return true;
	}

	if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]))
	{
		const vector<wstring> parts = componentsFrom(path, 2);
		if (parts.size() >= 2 && isLocalDevicePrefix(parts[0]) && (isDriveSpec(parts[1]) || isVolumeName(parts[1])))
		{
			out.root = L"\\\\" + parts[0] + L"\\" + parts[1] + L"\\";
			out.driveLetter = isDriveSpec(parts[1]) ? static_cast<wchar_t>(std::towupper(parts[1][0])) : 0;
			out.components.assign(parts.begin() + 2, parts.end());
			return true;
		}
	}

	return false;
}

// Win32's own lexical normalization: an absolute path with `.` and `..`
// folded and separators made backslashes. Relative, root-relative and
// drive-relative spellings are taken from the current directory the way an
// open takes them. No I/O.
wstring fullPathOf(const wstring& path)
{
	const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (needed == 0)
		return L"";
	wstring full(needed, L'\0');
	const DWORD written = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
	if (written == 0 || written >= needed)
		return L"";
	full.resize(written);
	return full;
}

wstring joined(const wstring& root, const vector<wstring>& components, size_t from)
{
	wstring path = root;
	for (size_t index = from; index < components.size(); ++index)
	{
		if (!path.empty() && !isSeparator(path.back()))
			path += L'\\';
		path += components[index];
	}
	return path;
}

enum class Problem
{
	None,
	OtherLink,
	TooManyLinks,
};

// Where a path leads. root is empty when every step stays on the local
// drives; otherwise it is remoteRoot's form, or `x:` for a network drive.
// link is the component whose link led off the local drives (empty when the
// path is spelled remote).
struct Destination
{
	wstring root;
	wstring link;
	Problem problem = Problem::None;
	wstring path;
};

Destination destinationOf(const wstring& path, const ConfigPathPolicy::FileSystem& fileSystem)
{
	wstring current = path;
	wstring lastLink;
	for (int links = 0; links <= ConfigPathPolicy::kLinkLimit; ++links)
	{
		wstring spelled = withoutNtPrefix(current);
		if (!ConfigPathPolicy::remoteRoot(spelled).empty())
			return {ConfigPathPolicy::remoteRoot(spelled), lastLink, Problem::None, spelled};
		// A verbatim path reaches the kernel as written; anything else is
		// folded first, and the walk has to see what the open will see.
		if (!isVerbatim(spelled))
			spelled = fullPathOf(spelled);

		LocalPath local;
		if (spelled.empty() || !splitLocal(spelled, local))
			return {};
		if (local.driveLetter != 0 && fileSystem.isNetworkDrive(local.driveLetter))
			return {wstring(1, static_cast<wchar_t>(std::towlower(local.driveLetter))) + L":", lastLink, Problem::None, spelled};

		if (!fileSystem.begin(local.root))
			return {};
		vector<wstring> walked;
		bool relinked = false;
		for (size_t index = 0; index < local.components.size() && !relinked; ++index)
		{
			const wstring& component = local.components[index];
			// Only a verbatim path still holds these, and the file system
			// refuses them as names: the open fails, there is nothing to judge.
			if (component == L"." || component == L"..")
				return {};
			walked.push_back(component);
			const wstring candidate = joined(local.root, walked, 0);
			const Entry entry = fileSystem.entry(candidate);
			switch (entry.kind)
			{
			case Entry::Kind::Missing:
				return {};
			case Entry::Kind::Plain:
				break;
			case Entry::Kind::OtherLink:
				return {L"", candidate, Problem::OtherLink};
			case Entry::Kind::Unexaminable:
			{
				if (fileSystem.strict())
					return {};
				// The one step that reaches the target: judge where the open
				// arrived.
				const wstring arrived = fileSystem.finalPath(joined(candidate, local.components, index + 1));
				const wstring arrivedRoot = arrived.empty() ? L"" : ConfigPathPolicy::remoteRoot(arrived);
				return {arrivedRoot, arrivedRoot.empty() ? L"" : candidate};
			}
			case Entry::Kind::Link:
			{
				if (entry.relative)
				{
					// Taken from the link's folder, with `..` folded the way
					// the kernel folds a relative link.
					vector<wstring> target(walked.begin(), walked.end() - 1);
					for (const wstring& part : componentsFrom(entry.target, 0))
					{
						if (part == L"..")
						{
							if (!target.empty())
								target.pop_back();
						}
						else if (part != L".")
							target.push_back(part);
					}
					target.insert(target.end(), local.components.begin() + static_cast<std::ptrdiff_t>(index) + 1,
						local.components.end());
					current = joined(local.root, target, 0);
				}
				else
					current = joined(entry.target, local.components, index + 1);
				lastLink = candidate;
				relinked = true;
				break;
			}
			}
		}
		if (!relinked)
			return {};
	}
	return {L"", lastLink, Problem::TooManyLinks};
}

// Not in the user-mode SDK headers (ntifs.h).
constexpr ULONG kSymlinkFlagRelative = 0x00000001;
constexpr DWORD kReparseBufferSize = 16 * 1024;

// The substitute name of a symbolic link's or a junction's reparse data, the
// target the kernel follows. REPARSE_DATA_BUFFER is read by offset: a
// 4-byte tag, a 2-byte length and 2 reserved bytes, then the substitute and
// print name offsets and lengths (2 bytes each), then for a symbolic link a
// 4-byte flags word, then the path buffer the offsets point into.
bool readLinkTarget(const unsigned char* data, DWORD size, Entry& out)
{
	if (size < 16)
		return false;
	ULONG tag = 0;
	std::memcpy(&tag, data, sizeof(tag));
	USHORT substituteOffset = 0;
	USHORT substituteLength = 0;
	std::memcpy(&substituteOffset, data + 8, sizeof(substituteOffset));
	std::memcpy(&substituteLength, data + 10, sizeof(substituteLength));

	size_t pathBuffer = 16;
	ULONG flags = 0;
	if (tag == IO_REPARSE_TAG_SYMLINK)
	{
		if (size < 20)
			return false;
		std::memcpy(&flags, data + 16, sizeof(flags));
		pathBuffer = 20;
	}
	if (substituteLength % sizeof(wchar_t) != 0 || pathBuffer + substituteOffset + substituteLength > size)
		return false;

	out.target.resize(substituteLength / sizeof(wchar_t));
	std::memcpy(out.target.data(), data + pathBuffer + substituteOffset, substituteLength);
	out.relative = (flags & kSymlinkFlagRelative) != 0;
	return !out.target.empty();
}

bool isMissingError(DWORD error)
{
	switch (error)
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_NAME:
	case ERROR_BAD_PATHNAME:
	case ERROR_DIRECTORY:
	case ERROR_INVALID_DRIVE:
	case ERROR_NOT_READY:
		return true;
	default:
		return false;
	}
}

class Win32FileSystem : public ConfigPathPolicy::FileSystem
{
public:
	bool strict() const override { return true; }

	bool begin(const wstring& root) const override
	{
		parent = nullptr;
		leafPath.clear();
		lastAttributesOnly = false;
		winutil::UniqueHandle handle(CreateFileW(root.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		if (!handle && GetLastError() == ERROR_ACCESS_DENIED)
		{
			handle.reset(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (handle)
			{
				++attributesOnlyPins;
				lastAttributesOnly = true;
			}
		}
		if (!handle)
		{
			error = GetLastError();
			return false;
		}
		FILE_ATTRIBUTE_TAG_INFO info = {};
		if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &info, sizeof(info))
			|| (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
			|| ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(info.ReparseTag)))
		{
			error = ERROR_CANT_ACCESS_FILE;
			return false;
		}
		parent = handle.get();
		pins.push_back(std::move(handle));
		leafPath = root;
		return true;
	}

	Entry entry(const wstring& path) const override
	{
		const size_t separator = path.find_last_of(L"\\/");
		const wstring component = path.substr(separator == wstring::npos ? 0 : separator + 1);
		Entry result;
		winutil::UniqueHandle handle = openChild(parent, component);
		if (!handle)
		{
			result.kind = isMissingError(error) ? Entry::Kind::Missing : Entry::Kind::Unexaminable;
			parent = nullptr;
			return result;
		}
		parent = handle.get();
		pins.push_back(std::move(handle));
		leafPath = path;
		FILE_ATTRIBUTE_TAG_INFO tagInfo = {};
		if (!GetFileInformationByHandleEx(parent, FileAttributeTagInfo, &tagInfo, sizeof(tagInfo)))
		{
			error = GetLastError();
			result.kind = Entry::Kind::Unexaminable;
			return result;
		}
		if ((tagInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 || !IsReparseTagNameSurrogate(tagInfo.ReparseTag))
		{
			result.kind = Entry::Kind::Plain;
			return result;
		}
		if (tagInfo.ReparseTag != IO_REPARSE_TAG_SYMLINK && tagInfo.ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
		{
			result.kind = Entry::Kind::OtherLink;
			return result;
		}
		vector<unsigned char> buffer(kReparseBufferSize);
		DWORD returned = 0;
		if (!DeviceIoControl(parent, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
			static_cast<DWORD>(buffer.size()), &returned, nullptr) || !readLinkTarget(buffer.data(), returned, result))
		{
			error = ERROR_CANT_ACCESS_FILE;
			result.kind = Entry::Kind::Unexaminable;
			return result;
		}
		result.kind = Entry::Kind::Link;
		return result;
	}

	wstring finalPath(const wstring&) const override { return L""; }

	bool isNetworkDrive(wchar_t driveLetter) const override
	{
		const wchar_t root[] = {driveLetter, L':', L'\\', L'\0'};
		return GetDriveTypeW(root) == DRIVE_REMOTE;
	}

	// No name-based enumeration: Contents and the architecture directory are
	// judged first, then their pinned directory handle supplies the module name.
	wstring moduleName() const
	{
		alignas(FILE_ID_BOTH_DIR_INFO) unsigned char buffer[16384];
		bool first = true;
		for (;;)
		{
			if (!GetFileInformationByHandleEx(parent, first ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo,
				buffer, sizeof(buffer)))
			{
				error = GetLastError();
				if (error == ERROR_NO_MORE_FILES)
					error = ERROR_FILE_NOT_FOUND;
				return L"";
			}
			first = false;
			size_t offset = 0;
			for (;;)
			{
				const auto* info = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer + offset);
				const size_t header = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
				if (offset + header > sizeof(buffer) || info->FileNameLength > sizeof(buffer) - offset - header
					|| info->FileNameLength % sizeof(wchar_t) != 0)
				{
					error = ERROR_INVALID_DATA;
					return L"";
				}
				wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
				if (name.size() >= 5 && lowered(name.substr(name.size() - 5)) == L".vst3")
					return name;
				if (info->NextEntryOffset == 0)
					break;
				if (info->NextEntryOffset < header || info->NextEntryOffset > sizeof(buffer) - offset - header
					|| info->NextEntryOffset % alignof(FILE_ID_BOTH_DIR_INFO) != 0)
				{
					error = ERROR_INVALID_DATA;
					return L"";
				}
				offset += info->NextEntryOffset;
			}
		}
	}

	mutable vector<winutil::UniqueHandle> pins;
	mutable HANDLE parent = nullptr;
	mutable wstring leafPath;
	mutable DWORD error = ERROR_SUCCESS;
	mutable size_t attributesOnlyPins = 0;
	// Whether the handle in parent was opened attributes-only. A leaf file
	// held that way cannot be read, so judge() refuses it as before.
	mutable bool lastAttributesOnly = false;

private:
	winutil::UniqueHandle openChild(HANDLE directory, const wstring& component) const
	{
		// winternl.h provides the ABI; load exports like ProcessCommandLine does.
		static const winutil::UniqueModule module(LoadLibraryW(L"ntdll.dll"));
		static const auto create = reinterpret_cast<decltype(&NtCreateFile)>(GetProcAddress(module.get(), "NtCreateFile"));
		static const auto toDos = reinterpret_cast<decltype(&RtlNtStatusToDosError)>(GetProcAddress(module.get(), "RtlNtStatusToDosError"));
		lastAttributesOnly = false;
		if (create == nullptr || toDos == nullptr || directory == nullptr
			|| component.empty() || component.size() > (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t))
		{
			error = ERROR_INVALID_NAME;
			return {};
		}
		UNICODE_STRING name = {};
		name.Buffer = const_cast<wchar_t*>(component.data());
		name.Length = static_cast<USHORT>(component.size() * sizeof(wchar_t));
		name.MaximumLength = name.Length;
		OBJECT_ATTRIBUTES attributes = {};
		attributes.Length = sizeof(attributes);
		attributes.RootDirectory = directory;
		attributes.ObjectName = &name;
		attributes.Attributes = OBJ_CASE_INSENSITIVE;
		IO_STATUS_BLOCK statusBlock = {};
		HANDLE result = nullptr;
		// FILE_READ_DATA and FILE_LIST_DIRECTORY are the same access bit. Unlike
		// attributes-only opens this takes part in delete-sharing checks.
		NTSTATUS status = create(&result, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			&attributes, &statusBlock, nullptr, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
			FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
		if (status < 0 && toDos(status) == ERROR_ACCESS_DENIED)
		{
			// The engine runs as LOCAL SERVICE, which may not list C:\Users\<name>,
			// AppData or Local but reaches the configuration folder below them
			// through bypass-traverse checking (SeChangeNotifyPrivilege, which
			// every service token holds) and the grants on that folder. So a
			// component it may not list is held with FILE_READ_ATTRIBUTES only;
			// a child opened relative to that handle is checked against the
			// child's own DACL, and the parent's traverse right is skipped only
			// under that privilege (EngineOrchestrationTests proves both).
			//
			// What this does to the guarantees. Readers are unaffected: every
			// component is still opened relative to the handle before it, and
			// the bytes come from the leaf handle; a handle follows the object,
			// not the name, so nothing done to an ancestor's name redirects a
			// chain that is already open. An attributes-only open takes no part
			// in share checks, so on its own it would not stop a rename of that
			// folder. It is never on its own: the chain also holds the next
			// component and the leaf below it, and NTFS refuses to rename a
			// directory with open handles beneath it (ERROR_ACCESS_DENIED for
			// MoveFileW and for a POSIX-semantics rename, measured on Windows 11
			// 22621 in EngineOrchestrationTests), and a folder cannot be
			// deleted while it holds anything. That is file-system behaviour,
			// not a share mode; it was not measured on ReFS or a Dev Drive.
			status = create(&result, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
				&attributes, &statusBlock, nullptr, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
				FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
			if (status >= 0)
			{
				++attributesOnlyPins;
				lastAttributesOnly = true;
			}
		}
		if (status < 0)
		{
			error = toDos(status);
			return {};
		}
		return winutil::UniqueHandle(result);
	}
};

const wchar_t* const kCopyAdvice = L"the audio engine only opens files on local drives, so copy the file into the configuration folder";

// A link as the user knows it: the walk reaches a link's target in its
// verbatim spelling (\\?\D:\x), which names the same file as D:\x.
wstring displayed(const wstring& path)
{
	if (isVerbatim(path) && path.size() >= 7 && isDriveSpec(path.substr(4, 2)) && isSeparator(path[6]))
		return path.substr(4);
	return path;
}

bool acceptsDestination(const wstring& path, const Destination& destination, const wstring& configRoot, wstring& reason)
{
	switch (destination.problem)
	{
	case Problem::OtherLink:
		reason = L"\"" + path + L"\" leads through \"" + displayed(destination.link)
			+ L"\", a kind of link the audio engine does not follow, so copy the file into the configuration folder";
		return false;
	case Problem::TooManyLinks:
		reason = L"\"" + path + L"\" leads through more than " + std::to_wstring(ConfigPathPolicy::kLinkLimit)
			+ L" links, so copy the file into the configuration folder";
		return false;
	case Problem::None:
		break;
	}

	if (destination.root.empty())
		return true;
	// The configuration's own place is judged the same way, so a config
	// reached through a link to a share may still name that share.
	if (destination.root == configRoot)
		return true;

	if (destination.link.empty())
		reason = L"\"" + path + L"\" is on a network share or a device path; " + kCopyAdvice;
	else
		reason = L"\"" + path + L"\" leads through \"" + displayed(destination.link) + L"\" to a network share or a device path; "
			+ kCopyAdvice;
	return false;
}

}

wstring ConfigPathPolicy::remoteRoot(const wstring& path)
{
	const wstring spelled = withoutNtPrefix(path);
	if (spelled.size() < 2 || !isSeparator(spelled[0]) || !isSeparator(spelled[1]))
		return L"";

	const vector<wstring> components = componentsFrom(spelled, 2);
	size_t first = 0;
	if (components.size() >= 2 && isLocalDevicePrefix(components[0]))
	{
		if (isDriveSpec(components[1]) || isVolumeName(components[1]))
			return L"";
		// \\?\UNC\host\share is \\host\share, so a configuration on a share
		// matches its references however either is spelled.
		if (lowered(components[1]) == L"unc")
			first = 2;
	}

	wstring root = L"\\\\";
	if (first < components.size())
		root += components[first];
	if (first + 1 < components.size())
		root += L"\\" + components[first + 1];
	return lowered(root);
}

bool ConfigPathPolicy::allowsOpen(const wstring& path, const wstring& configPath, wstring& reason)
{
	const Win32FileSystem fileSystem;
	return allowsOpen(path, configPath, reason, fileSystem);
}

bool ConfigPathPolicy::allowsOpen(const wstring& path, const wstring& configPath, wstring& reason,
	const FileSystem& fileSystem)
{
	const Destination destination = destinationOf(path, fileSystem);
	return acceptsDestination(path, destination, destination.root.empty() || configPath.empty() ? L"" : destinationOf(configPath, fileSystem).root, reason);
}

ConfigFileReference::Target ConfigPathPolicy::judge(const wstring& path, const wstring& configPath, bool library)
{
	ConfigFileReference::Target result;
	if (path.empty())
		return result;
	Win32FileSystem fileSystem;
	const Destination destination = destinationOf(path, fileSystem);
	// Keep the same remote-root and link diagnostics as the table-driven policy.
	if (destination.problem != Problem::None || !destination.root.empty())
	{
		Win32FileSystem check;
		const wstring configRoot = configPath.empty() ? L"" : destinationOf(configPath, check).root;
		if (!acceptsDestination(path, destination, configRoot, result.refusal))
			return result;
		// A same-share reference is the deliberate exception. Open its share
		// root only after judgment, then use the same handle-relative walk.
		if (!destination.root.empty())
		{
			LocalPath local;
			wstring root;
			vector<wstring> parts;
			if (splitLocal(destination.path, local))
			{
				root = local.root;
				parts = local.components;
			}
			else
			{
				parts = componentsFrom(destination.path, 2);
				if (parts.size() >= 2 && isLocalDevicePrefix(parts[0]) && lowered(parts[1]) == L"unc")
					parts.erase(parts.begin(), parts.begin() + 2);
				if (parts.size() < 2)
					return result;
				root = L"\\\\" + parts[0] + L"\\" + parts[1] + L"\\";
				parts.erase(parts.begin(), parts.begin() + 2);
			}
			if (fileSystem.begin(root))
			{
				wstring candidate = root;
				for (const auto& part : parts)
				{
					candidate = joined(candidate, {part}, 0);
					const Entry entry = fileSystem.entry(candidate);
					if (entry.kind != Entry::Kind::Plain)
					{
						if (entry.kind == Entry::Kind::Link || entry.kind == Entry::Kind::OtherLink)
							fileSystem.error = ERROR_CANT_ACCESS_FILE;
						break;
					}
				}
			}
		}
	}
	if (fileSystem.error == ERROR_SUCCESS && fileSystem.parent != nullptr && library)
	{
		FILE_ATTRIBUTE_TAG_INFO info = {};
		if (!GetFileInformationByHandleEx(fileSystem.parent, FileAttributeTagInfo, &info, sizeof(info)))
			fileSystem.error = GetLastError();
		else if ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
			&& path.size() >= 5 && lowered(path.substr(path.size() - 5)) == L".vst3")
		{
#if defined(_M_ARM64)
			const wchar_t* platform = L"arm64-win";
#elif defined(_WIN64)
			const wchar_t* platform = L"x86_64-win";
#else
			const wchar_t* platform = L"x86-win";
#endif
			const wstring folder = joined(fileSystem.leafPath, {L"Contents", platform}, 0);
			// Apply the same local-link and same-share rules to bundle children.
			// Keep the earlier bundle pins while acquiring the next judged chain.
			auto keep = [&](ConfigFileReference::Target next) {
				if (!next.refusal.empty())
					result.refusal = std::move(next.refusal);
				fileSystem.error = next.error;
				fileSystem.parent = next.path.file;
				fileSystem.leafPath = next.path.name;
				fileSystem.attributesOnlyPins += next.path.attributesOnlyPins;
				// The nested judge already refused an unreadable leaf of its own.
				fileSystem.lastAttributesOnly = false;
				for (auto& pin : next.path.pins)
					fileSystem.pins.push_back(std::move(pin));
			};
			keep(judge(folder, configPath));
			if (result.refusal.empty() && fileSystem.error == ERROR_SUCCESS && fileSystem.parent != nullptr)
			{
				const wstring module = fileSystem.moduleName();
				if (!module.empty())
					keep(judge(joined(fileSystem.leafPath, {module}, 0), configPath));
			}
		}
	}
	// Ancestors may be attributes-only, the file itself may not: the readers
	// and LoadLibraryW need its data, and a leaf the service cannot read stays
	// refused as it was before the fallback existed. A directory leaf (a .vst3
	// bundle before its module is found) is left to the caller.
	if (fileSystem.error == ERROR_SUCCESS && fileSystem.parent != nullptr && fileSystem.lastAttributesOnly)
	{
		FILE_ATTRIBUTE_TAG_INFO info = {};
		if (!GetFileInformationByHandleEx(fileSystem.parent, FileAttributeTagInfo, &info, sizeof(info))
			|| (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			fileSystem.error = ERROR_ACCESS_DENIED;
	}
	result.error = fileSystem.error;
	if (result.error == ERROR_SHARING_VIOLATION)
		result.refusal = L"the file or its folder is in use by another program; try again";
	else if (result.error != ERROR_SUCCESS && !isMissingError(result.error))
		result.refusal = L"the file or its folder could not be checked safely; check its permissions and try again";
	if (!result.refusal.empty())
		return result;
	// Missing files retain their judged spelling for the existing missing-file
	// diagnostics, but never acquire a handle or retry an open by name.
	const HANDLE leaf = result.error == ERROR_SUCCESS ? fileSystem.parent : nullptr;
	result.path = JudgedPath(fileSystem.leafPath.empty() || leaf == nullptr ? path : fileSystem.leafPath,
		std::move(fileSystem.pins), leaf, fileSystem.attributesOnlyPins);
	return result;
}
