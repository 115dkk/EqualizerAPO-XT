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
};

Destination destinationOf(const wstring& path, const ConfigPathPolicy::FileSystem& fileSystem)
{
	wstring current = path;
	wstring lastLink;
	for (int links = 0; links <= ConfigPathPolicy::kLinkLimit; ++links)
	{
		wstring spelled = withoutNtPrefix(current);
		if (!ConfigPathPolicy::remoteRoot(spelled).empty())
			return {ConfigPathPolicy::remoteRoot(spelled), lastLink};
		// A verbatim path reaches the kernel as written; anything else is
		// folded first, and the walk has to see what the open will see.
		if (!isVerbatim(spelled))
			spelled = fullPathOf(spelled);

		LocalPath local;
		if (spelled.empty() || !splitLocal(spelled, local))
			return {};
		if (local.driveLetter != 0 && fileSystem.isNetworkDrive(local.driveLetter))
			return {wstring(1, static_cast<wchar_t>(std::towlower(local.driveLetter))) + L":", lastLink};

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
	Entry entry(const wstring& path) const override
	{
		Entry result;
		// FILE_FLAG_OPEN_REPARSE_POINT opens a link itself rather than what
		// it points at, so reading one never reaches its target.
		const winutil::UniqueHandle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
		if (!handle)
		{
			result.kind = isMissingError(GetLastError()) ? Entry::Kind::Missing : Entry::Kind::Unexaminable;
			return result;
		}

		FILE_ATTRIBUTE_TAG_INFO tagInfo = {};
		if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tagInfo, sizeof(tagInfo)))
		{
			result.kind = Entry::Kind::Unexaminable;
			return result;
		}
		// A reparse point that is not a name surrogate (a cloud placeholder,
		// a deduplicated file) is the file itself, not a way elsewhere.
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
		if (!DeviceIoControl(handle.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
				static_cast<DWORD>(buffer.size()), &returned, nullptr)
			|| !readLinkTarget(buffer.data(), returned, result))
		{
			result.kind = Entry::Kind::Unexaminable;
			return result;
		}
		result.kind = Entry::Kind::Link;
		return result;
	}

	wstring finalPath(const wstring& path) const override
	{
		const winutil::UniqueHandle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr));
		if (!handle)
			return L"";

		// A volume without a drive letter has no DOS name; its GUID name is
		// just as local.
		for (const DWORD volumeName : {VOLUME_NAME_DOS, VOLUME_NAME_GUID})
		{
			const DWORD flags = FILE_NAME_NORMALIZED | volumeName;
			const DWORD needed = GetFinalPathNameByHandleW(handle.get(), nullptr, 0, flags);
			if (needed == 0)
				continue;
			wstring result(needed, L'\0');
			const DWORD written = GetFinalPathNameByHandleW(handle.get(), result.data(), needed, flags);
			if (written == 0 || written >= needed)
				continue;
			result.resize(written);
			return result;
		}
		return L"";
	}

	bool isNetworkDrive(wchar_t driveLetter) const override
	{
		const wchar_t root[] = {driveLetter, L':', L'\\', L'\0'};
		return GetDriveTypeW(root) == DRIVE_REMOTE;
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
	static const Win32FileSystem fileSystem;
	return allowsOpen(path, configPath, reason, fileSystem);
}

bool ConfigPathPolicy::allowsOpen(const wstring& path, const wstring& configPath, wstring& reason,
	const FileSystem& fileSystem)
{
	const Destination destination = destinationOf(path, fileSystem);
	switch (destination.problem)
	{
	case Problem::OtherLink:
		reason = L"\"" + path + L"\" leads through \"" + displayed(destination.link)
			+ L"\", a kind of link the audio engine does not follow, so copy the file into the configuration folder";
		return false;
	case Problem::TooManyLinks:
		reason = L"\"" + path + L"\" leads through more than " + std::to_wstring(kLinkLimit)
			+ L" links, so copy the file into the configuration folder";
		return false;
	case Problem::None:
		break;
	}

	if (destination.root.empty())
		return true;
	// The configuration's own place is judged the same way, so a config
	// reached through a link to a share may still name that share.
	if (!configPath.empty() && destination.root == destinationOf(configPath, fileSystem).root)
		return true;

	if (destination.link.empty())
		reason = L"\"" + path + L"\" is on a network share or a device path; " + kCopyAdvice;
	else
		reason = L"\"" + path + L"\" leads through \"" + displayed(destination.link) + L"\" to a network share or a device path; "
			+ kCopyAdvice;
	return false;
}
