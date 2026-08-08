/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The VST chunk path scan: decode a plugin's base64 state chunk and find
	absolute Windows paths inside it, so the editors can warn when a plugin
	references files the audio service cannot read. Audit #250 B5: the scan
	lived as two verbatim copies in the modern VST card and the frozen legacy
	VST GUI; a permission rule that must land twice tends to land once.
	QtCore-only, so the candidate extraction is unit-testable.
*/

#pragma once

#include <string>

#include <QStringList>

// Absolute path spellings found inside the decoded chunk text, in match
// order, duplicates included. Oversized chunks (>= 100000 wide characters)
// are skipped entirely - state blobs that large are audio data, not a path
// list worth scanning. No filesystem access.
QStringList vstChunkPathCandidates(const std::wstring& chunkData);

// The warning list: candidates that exist on disk but are not readable by
// the audio service account, deduplicated.
QStringList vstChunkUnreadablePaths(const std::wstring& chunkData);
