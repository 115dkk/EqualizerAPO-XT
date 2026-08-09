#pragma once

#include <new>
#include <utility>
#include <winerror.h>

class ComBoundary
{
public:
	template <typename Function>
	static HRESULT invoke(Function&& function)
	{
		try
		{
			return std::forward<Function>(function)();
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
	}
};
