#pragma once

#include <fftw3.h>
#include <malloc.h>

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

// 64-byte-aligned single allocation for buffers FFTW plans never touch (the
// partition slabs). fftw_malloc only promises FFTW's own SIMD alignment, so
// the slabs use _aligned_malloc directly to guarantee cache-line-aligned
// plane starts.
template<typename T>
struct HcSlabRelease
{
	void operator()(T* ptr) const
	{
		_aligned_free(ptr);
	}
};

template<typename T>
using HcSlabPtr = std::unique_ptr<T, HcSlabRelease<T>>;

template<typename T>
HcSlabPtr<T> makeHcSlab(std::size_t count)
{
	return HcSlabPtr<T>(static_cast<T*>(_aligned_malloc(sizeof(T) * count, 64)));
}
