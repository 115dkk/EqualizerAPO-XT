#ifndef SCOPEGUARD_H
#define SCOPEGUARD_H

// Taken from "Declarative Control Flow" (CppCon 2015)
// adjusted to work with Visual C++ 2013

#include <exception>
#include <utility>
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#define noexcept
#endif
#include "runtime/lifetime/UncaughtExceptions.h"
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#undef noexcept
#endif

#define CONCATENATE_IMPL(s1, s2) s1 ## s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#ifdef __COUNTER__
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __COUNTER__)
#else
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __LINE__)
#endif

namespace detail {
enum class ScopeGuardOnExit
{
};

enum class ScopeGuardOnFail
{
};

enum class ScopeGuardOnSuccess
{
};

template<typename Fun> class ScopeGuard
{
public:
	ScopeGuard(Fun&& fn)
		: fn(std::move(fn))
	{
	}

	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard& operator=(const ScopeGuard&) = delete;

	ScopeGuard(ScopeGuard&& other) noexcept(noexcept(Fun(std::move(other.fn))))
		: fn(std::move(other.fn)), active(other.active)
	{
		other.dismiss();
	}

	ScopeGuard& operator=(ScopeGuard&&) = delete;

	~ScopeGuard() noexcept
	{
		if (active)
			fn();
	}

	void dismiss() noexcept { active = false; }

private:
	Fun fn;
	bool active = true;
};

class UncaughtExceptionCounter
{
	int exceptionCount_;

public:
	UncaughtExceptionCounter()
		: exceptionCount_(folly::uncaught_exceptions())
	{
	}

	bool newUncaughtException()
	{
		return folly::uncaught_exceptions() > exceptionCount_;
	}
};

template<typename FunctionType, bool executeOnException> class ScopeGuardForNewException
{
	FunctionType function_;
	UncaughtExceptionCounter ec_;

public:
	explicit ScopeGuardForNewException(const FunctionType& fn)
		: function_(fn)
	{
	}

	explicit ScopeGuardForNewException(FunctionType&& fn)
		: function_(std::move(fn))
	{
	}

	ScopeGuardForNewException(const ScopeGuardForNewException&) = delete;
	ScopeGuardForNewException& operator=(const ScopeGuardForNewException&) = delete;

	ScopeGuardForNewException(ScopeGuardForNewException&& other) noexcept(
		noexcept(FunctionType(std::move(other.function_))))
		: function_(std::move(other.function_)), ec_(other.ec_), active_(other.active_)
	{
		other.dismiss();
	}

	ScopeGuardForNewException& operator=(ScopeGuardForNewException&&) = delete;

	~ScopeGuardForNewException() noexcept
	{
		if (active_ && executeOnException == ec_.newUncaughtException())
		{
			function_();
		}
	}

	void dismiss() noexcept { active_ = false; }

private:
	bool active_ = true;
};

template<typename Fun> ScopeGuard<Fun> operator+(ScopeGuardOnExit, Fun&& fn)
{
	return ScopeGuard<Fun>(std::forward<Fun>(fn));
}

template<typename Fun> ScopeGuardForNewException<typename std::decay_t<Fun>, true> operator+(detail::ScopeGuardOnFail, Fun&& fn)
{
	return ScopeGuardForNewException<typename std::decay_t<Fun>, true>(std::forward<Fun>(fn));
}

template<typename Fun> ScopeGuardForNewException<typename std::decay_t<Fun>, false> operator+(detail::ScopeGuardOnSuccess, Fun&& fn)
{
	return ScopeGuardForNewException<typename std::decay_t<Fun>, false>(std::forward<Fun>(fn));
}
}

#define SCOPE_FAIL auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = ::detail::ScopeGuardOnFail() +[&]()
#define SCOPE_SUCCESS auto ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = ::detail::ScopeGuardOnSuccess() +[&]()
#define SCOPE_EXIT auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) = ::detail::ScopeGuardOnExit() +[&]()

#endif
