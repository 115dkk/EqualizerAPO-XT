/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// Audit #250 F020: the service name is what every restart path asks the SCM for.
inline constexpr wchar_t audioServiceName[] = L"AudioSrv";
inline constexpr wchar_t audioEndpointBuilderServiceName[] = L"AudioEndpointBuilder";
