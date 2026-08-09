/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2012  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <new>
#include "EqualizerAPO.h"
#include "../services/logging/LogHelper.h"
#include "ClassFactory.h"

long ClassFactory::lockCount = 0;

ClassFactory::ClassFactory()
{
	refCount = 1;
}

HRESULT __stdcall ClassFactory::QueryInterface(const IID& iid, void** ppv)
{
	if (iid == __uuidof(IUnknown) || iid == __uuidof(IClassFactory))
		*ppv = static_cast<IClassFactory*>(this);
	else
	{
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	reinterpret_cast<IUnknown*>(*ppv)->AddRef();
	return S_OK;
}

ULONG __stdcall ClassFactory::AddRef()
{
	return InterlockedIncrement(&refCount);
}

ULONG __stdcall ClassFactory::Release()
{
	const LONG remaining = InterlockedDecrement(&refCount);
	if (remaining == 0)
	{
		delete this;
		return 0;
	}

	return static_cast<ULONG>(remaining);
}

HRESULT __stdcall ClassFactory::CreateInstance(IUnknown* pUnknownOuter, const IID& iid, void** ppv)
{
	if (pUnknownOuter != nullptr && iid != __uuidof(IUnknown))
		return E_NOINTERFACE;

	// The throwing operator new never returns null, so the old null check was
	// dead code; the EqualizerAPO constructor (or a member constructor) can
	// throw instead, and no C++ exception may cross this COM boundary into
	// audiodg.exe. Translate to HRESULTs.
	try
	{
		EqualizerAPO* apo = new EqualizerAPO(pUnknownOuter);

		HRESULT hr = apo->NonDelegatingQueryInterface(iid, ppv);

		apo->NonDelegatingRelease();
		return hr;
	}
	catch (const std::bad_alloc&)
	{
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		return E_FAIL;
	}
}

HRESULT __stdcall ClassFactory::LockServer(BOOL bLock)
{
	if (bLock)
		InterlockedIncrement(&lockCount);
	else
		InterlockedDecrement(&lockCount);

	return S_OK;
}
