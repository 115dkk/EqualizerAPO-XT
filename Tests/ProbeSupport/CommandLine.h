/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	TCLAP adapter for the probes' Unicode command lines. The probes keep their
	existing usage text and error policy; this adapter only converts argv and
	lets parsing exceptions return to the caller.
*/

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/MultiSwitchArg.h>

namespace probesupport
{
	inline std::string utf8(const std::wstring& text)
	{
		if (text.empty())
			return {};
		const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			nullptr, 0, nullptr, nullptr);
		if (bytes <= 0)
			return {};
		std::string result(static_cast<size_t>(bytes), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			result.data(), bytes, nullptr, nullptr);
		return result;
	}

	inline std::wstring utf16(const std::string& text)
	{
		if (text.empty())
			return {};
		const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			nullptr, 0);
		if (characters <= 0)
			return {};
		std::wstring result(static_cast<size_t>(characters), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			result.data(), characters);
		return result;
	}

	class CommandLine
	{
	public:
		CommandLine()
			: command_("", ' ', "", false)
		{
			command_.setExceptionHandling(false);
			// CmdLine always installs a standalone "--" ignore-rest argument.
			// None of the probes accepted it before adopting TCLAP.
			command_.getArgList().remove_if([](const TCLAP::Arg* argument) {
				return argument->getName() == TCLAP::Arg::ignoreNameString();
			});
		}

		TCLAP::CmdLine& parser() noexcept
		{
			return command_;
		}

		bool parse(int argc, wchar_t** argv)
		{
			std::vector<std::string> arguments;
			arguments.reserve(static_cast<size_t>(argc));
			for (int i = 0; i < argc; i++)
				arguments.push_back(utf8(argv[i]));
			try
			{
				command_.parse(arguments);
				bool expectsValue = false;
				for (int i = 1; i < argc; i++)
				{
					const std::wstring token = argv[i];
					if (expectsValue)
					{
						expectsValue = false;
						continue;
					}
					// TCLAP treats an empty token and a lone "-" as empty combined
					// switches. The probes previously rejected both.
					if (token.empty() || token == L"-")
					{
						badArgument_ = token;
						return false;
					}
					if (token.starts_with(L"--"))
					{
						const std::string name = utf8(token.substr(2));
						for (const TCLAP::Arg* argument : command_.getArgList())
						{
							if (argument->getName() == name && argument->isValueRequired())
							{
								expectsValue = true;
								break;
							}
						}
					}
				}
				return true;
			}
			catch (const TCLAP::ArgException& error)
			{
				badArgument_ = utf16(error.argId());
				const std::wstring prefix = L"Argument: ";
				if (badArgument_.starts_with(prefix))
					badArgument_.erase(0, prefix.size());
				// Missing-value exceptions identify a long argument as "(--name)".
				// The old parsers reported the original token, "--name".
				if (badArgument_.size() > 2 && badArgument_.front() == L'(' && badArgument_.back() == L')')
					badArgument_ = badArgument_.substr(1, badArgument_.size() - 2);
				return false;
			}
		}

		const std::wstring& badArgument() const noexcept
		{
			return badArgument_;
		}

	private:
		TCLAP::CmdLine command_;
		std::wstring badArgument_;
	};

	inline std::wstring lastWide(const TCLAP::MultiArg<std::string>& argument)
	{
		const std::vector<std::string>& values = argument.getValue();
		return values.empty() ? std::wstring() : utf16(values.back());
	}

	class SetBoolVisitor final : public TCLAP::Visitor
	{
	public:
		SetBoolVisitor(bool& target, bool value) noexcept
			: target_(target), value_(value)
		{
		}

		void visit() override
		{
			target_ = value_;
		}

	private:
		bool& target_;
		bool value_;
	};
}
