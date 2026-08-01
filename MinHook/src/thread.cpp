/*
 *  MinHook - Minimalistic API Hook Library
 *  Copyright (C) 2009 Tsuda Kageyu. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 *  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cassert>
#include <new>
#include <vector>
#include <windows.h>
#include <TlHelp32.h>

#include "thread.h"

namespace MinHook { namespace
{
	class ScopedHandle
	{
	private:
		HANDLE handle_;
	public:
		explicit ScopedHandle(HANDLE handle)
			: handle_(handle)
		{
		}

		~ScopedHandle()
		{
			if (handle_ != NULL && handle_ != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle_);
			}
		}

		bool IsValid() const
		{
			return handle_ != NULL && handle_ != INVALID_HANDLE_VALUE;
		}

		operator HANDLE() const
		{
			return handle_;
		}
	private:
		ScopedHandle(const ScopedHandle&);
		const ScopedHandle& operator=(const ScopedHandle&);
	};

}}

namespace MinHook
{
	CriticalSection::CriticalSection()
	{
		InitializeCriticalSection(&cs_);
	}

	CriticalSection::~CriticalSection()
	{
		DeleteCriticalSection(&cs_);
	}

	void CriticalSection::enter()
	{
		EnterCriticalSection(&cs_);
	}

	void CriticalSection::leave()
	{
		LeaveCriticalSection(&cs_);
	}

	CriticalSection::ScopedLock::ScopedLock(CriticalSection& cs)
		: cs_(cs)
	{
		cs_.enter();
	}

	CriticalSection::ScopedLock::~ScopedLock()
	{
		cs_.leave();
	}
}

namespace MinHook
{
	ScopedThreadExclusive::ScopedThreadExclusive(
		const std::vector<uintptr_t>& oldIPs,
		const std::vector<uintptr_t>& newIPs)
		: acquired_(false)
	{
		assert(("ScopedThreadExclusive::ctor", (oldIPs.size() == newIPs.size())));

		std::vector<DWORD> threads;
		if (!GetThreads(threads))
		{
			return;
		}

		acquired_ = Freeze(threads, suspendedThreads_, oldIPs, newIPs);
	}

	ScopedThreadExclusive::~ScopedThreadExclusive()
	{
		Unfreeze(suspendedThreads_);
	}

	bool ScopedThreadExclusive::IsAcquired() const
	{
		return acquired_;
	}

	bool ScopedThreadExclusive::GetThreads(std::vector<DWORD>& threads)
	{
		ScopedHandle hSnapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
		if (!hSnapshot.IsValid())
		{
			return false;
		}

		const DWORD currentProcessId = GetCurrentProcessId();
		const DWORD currentThreadId = GetCurrentThreadId();
		THREADENTRY32 te = { sizeof(te) };
		if (!Thread32First(hSnapshot, &te))
		{
			return false;
		}

		for (;;)
		{
			if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD)
				&& te.th32OwnerProcessID == currentProcessId
				&& te.th32ThreadID != currentThreadId)
			{
				try
				{
					threads.push_back(te.th32ThreadID);
				}
				catch (const std::bad_alloc&)
				{
					return false;
				}
			}

			te.dwSize = sizeof(te);
			if (!Thread32Next(hSnapshot, &te))
			{
				break;
			}
		}

		return GetLastError() == ERROR_NO_MORE_FILES;
	}

	bool ScopedThreadExclusive::Freeze(
		const std::vector<DWORD>& threads,
		std::vector<DWORD>& suspendedThreads,
		const std::vector<uintptr_t>& oldIPs,
		const std::vector<uintptr_t>& newIPs)
	{
		assert(("ScopedThreadExclusive::Freeze", (oldIPs.size() == newIPs.size())));

		try
		{
			suspendedThreads.reserve(threads.size());
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}

		static const DWORD ThreadAccess = THREAD_SUSPEND_RESUME
			| THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SET_CONTEXT;

		for (std::vector<DWORD>::const_iterator tid = threads.begin(); tid != threads.end(); ++tid)
		{
			ScopedHandle hThread(OpenThread(ThreadAccess, FALSE, *tid));
			if (!hThread.IsValid())
			{
				continue;
			}

			if (SuspendThread(hThread) == static_cast<DWORD>(-1))
			{
				continue;
			}

			// Track only threads that this scope actually suspended.
			suspendedThreads.push_back(*tid);

			CONTEXT context = { 0 };
			context.ContextFlags = CONTEXT_CONTROL;
			if (!GetThreadContext(hThread, &context))
			{
				continue;
			}

#if defined _M_X64
			DWORD64& ip = context.Rip;
#elif defined _M_IX86
			DWORD& ip = context.Eip;
#endif
			bool changed = false;
			for (size_t i = 0; i < oldIPs.size(); ++i)
			{
				if (ip == oldIPs[i])
				{
					ip = newIPs[i];
					changed = true;
					break;
				}
			}

			if (changed)
			{
				SetThreadContext(hThread, &context);
			}
		}

		return true;
	}

	void ScopedThreadExclusive::Unfreeze(const std::vector<DWORD>& suspendedThreads)
	{
		for (std::vector<DWORD>::const_iterator tid = suspendedThreads.begin();
			tid != suspendedThreads.end(); ++tid)
		{
			ScopedHandle hThread(OpenThread(THREAD_SUSPEND_RESUME, FALSE, *tid));
			if (hThread.IsValid())
			{
				ResumeThread(hThread);
			}
		}
	}
}
