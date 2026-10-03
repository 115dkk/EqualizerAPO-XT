/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/IFilterFactory.h"
#include "filters/SendLink.h"

class FilterEngine;

class SendFilterFactory : public ParseReportingFactory
{
public:
	SendFilterFactory() = default;
	~SendFilterFactory() override = default;

	void initialize(FilterEngine* engine) override;
	FilterVector startOfConfiguration() override;
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command,
		std::wstring& parameters) override;
	FilterVector endOfConfiguration() override;

private:
	void reportReceiverNews(const SendReceiverNews& news);

	FilterEngine* engine_ = nullptr;
	SendReceiver receiver_;
	bool receiverInstalled_ = false;
	bool missingPostMixLogged_ = false;
	std::map<std::wstring, std::unique_ptr<SendWriter>> writers_;
	std::set<std::wstring> targetsInLoad_;
	std::vector<uint32_t> compensationDelays_;
	std::wstring ownGuid_;
};
