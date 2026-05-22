#pragma once

#include <fftw3.h>

#include <cstddef>
#include <memory>

template<typename T>
struct HcAlignedRelease
{
	void operator()(T* ptr) const
	{
		fftw_free(ptr);
	}
};

template<typename T>
using HcAlignedPtr = std::unique_ptr<T, HcAlignedRelease<T>>;

template<typename T>
HcAlignedPtr<T> makeHcAlignedArray(std::size_t count)
{
	return HcAlignedPtr<T>(static_cast<T*>(fftw_malloc(sizeof(T) * count)));
}
