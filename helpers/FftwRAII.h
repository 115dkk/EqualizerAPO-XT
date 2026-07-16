#pragma once

#include <memory>
#include <new>
#include <stdexcept>

#include <fftw3.h>

namespace fftw
{
	struct BufferDeleter
	{
		template<class T>
		void operator()(T* ptr) const noexcept
		{
			fftw_free(ptr);
		}
	};

	struct PlanDeleter
	{
		void operator()(fftw_plan_s* plan) const noexcept
		{
			if (plan != nullptr)
				fftw_destroy_plan(plan);
		}
	};

	using RealBuffer = std::unique_ptr<double, BufferDeleter>;
	using ComplexBuffer = std::unique_ptr<fftw_complex, BufferDeleter>;
	using Plan = std::unique_ptr<fftw_plan_s, PlanDeleter>;

	inline RealBuffer allocateReal(size_t count)
	{
		RealBuffer result(fftw_alloc_real(count));
		if (!result)
			throw std::bad_alloc();
		return result;
	}

	inline ComplexBuffer allocateComplex(size_t count)
	{
		ComplexBuffer result(fftw_alloc_complex(count));
		if (!result)
			throw std::bad_alloc();
		return result;
	}

	inline Plan makeRealToComplexPlan(int length, double* input, fftw_complex* output, unsigned flags = FFTW_ESTIMATE)
	{
		Plan result(fftw_plan_dft_r2c_1d(length, input, output, flags));
		if (!result)
			throw std::runtime_error("Could not create FFTW real-to-complex plan");
		return result;
	}

	inline Plan makeComplexToRealPlan(int length, fftw_complex* input, double* output, unsigned flags = FFTW_ESTIMATE)
	{
		Plan result(fftw_plan_dft_c2r_1d(length, input, output, flags));
		if (!result)
			throw std::runtime_error("Could not create FFTW complex-to-real plan");
		return result;
	}

	inline Plan makeComplexPlan(int length, fftw_complex* input, fftw_complex* output,
		int direction, unsigned flags = FFTW_ESTIMATE)
	{
		Plan result(fftw_plan_dft_1d(length, input, output, direction, flags));
		if (!result)
			throw std::runtime_error("Could not create FFTW complex plan");
		return result;
	}
}
