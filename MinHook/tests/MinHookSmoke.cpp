#include <windows.h>
#include <cstdio>

#include "..\MinHook.h"

namespace
{
	typedef int (WINAPI* TargetFunction)(int);

	TargetFunction g_original = NULL;
	volatile LONG g_stopWorkers = 0;
	volatile LONG g_invalidResults = 0;

	#pragma optimize("", off)
	__declspec(noinline) int WINAPI Target(int value)
	{
		return value + 1;
	}

	__declspec(noinline) int WINAPI TargetWithoutOriginal(int value)
	{
		return value + 2;
	}

	int WINAPI Detour(int value)
	{
		return g_original(value) + 100;
	}

	int WINAPI DetourWithoutOriginal(int value)
	{
		return value + 200;
	}
	#pragma optimize("", on)

	DWORD WINAPI WorkerThread(void*)
	{
		while (InterlockedCompareExchange(&g_stopWorkers, 0, 0) == 0)
		{
			const int result = Target(41);
			if (result != 42 && result != 142)
			{
				InterlockedIncrement(&g_invalidResults);
			}
		}

		return 0;
	}

	bool CheckStatus(const char* operation, MH_STATUS actual, MH_STATUS expected)
	{
		if (actual == expected)
		{
			return true;
		}

		std::printf("%s failed: expected %d, got %d\n", operation, expected, actual);
		return false;
	}
}

int main()
{
	std::setvbuf(stdout, NULL, _IONBF, 0);
	std::puts("phase: baseline");
	if (Target(41) != 42 || TargetWithoutOriginal(40) != 42)
	{
		std::puts("Baseline calls failed.");
		return 1;
	}

	std::puts("phase: initialize");
	if (!CheckStatus("MH_Initialize", MH_Initialize(), MH_OK))
	{
		return 2;
	}

	void* original = NULL;
	std::puts("phase: create hooks");
	if (!CheckStatus("MH_CreateHook(Target)",
		MH_CreateHook(reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour), &original), MH_OK))
	{
		return 3;
	}
	g_original = reinterpret_cast<TargetFunction>(original);

	if (!CheckStatus("MH_CreateHook(TargetWithoutOriginal)",
		MH_CreateHook(reinterpret_cast<void*>(&TargetWithoutOriginal),
			reinterpret_cast<void*>(&DetourWithoutOriginal), NULL), MH_OK))
	{
		return 4;
	}

	std::puts("phase: sequential enable/disable");
	if (!CheckStatus("MH_EnableHook(Target)", MH_EnableHook(reinterpret_cast<void*>(&Target)), MH_OK)
		|| Target(41) != 142
		|| !CheckStatus("MH_DisableHook(Target)", MH_DisableHook(reinterpret_cast<void*>(&Target)), MH_OK)
		|| Target(41) != 42)
	{
		return 5;
	}

	std::puts("phase: start workers");
	const DWORD workerCount = 4;
	HANDLE workers[workerCount] = { NULL };
	for (DWORD i = 0; i < workerCount; ++i)
	{
		workers[i] = CreateThread(NULL, 0, &WorkerThread, NULL, 0, NULL);
		if (workers[i] == NULL)
		{
			std::puts("CreateThread failed.");
			return 6;
		}
	}

	std::puts("phase: concurrent enable/disable");
	for (int i = 0; i < 50; ++i)
	{
		if (!CheckStatus("MH_EnableHook(Target)", MH_EnableHook(reinterpret_cast<void*>(&Target)), MH_OK)
			|| Target(41) != 142
			)
		{
			return 7;
		}
		if (!CheckStatus("MH_DisableHook(Target)", MH_DisableHook(reinterpret_cast<void*>(&Target)), MH_OK)
			|| Target(41) != 42)
		{
			return 7;
		}
	}

	std::puts("phase: null-original hook");
	if (!CheckStatus("MH_EnableHook(TargetWithoutOriginal)",
		MH_EnableHook(reinterpret_cast<void*>(&TargetWithoutOriginal)), MH_OK)
		|| TargetWithoutOriginal(40) != 240
		|| !CheckStatus("MH_DisableHook(TargetWithoutOriginal)",
			MH_DisableHook(reinterpret_cast<void*>(&TargetWithoutOriginal)), MH_OK)
		|| TargetWithoutOriginal(40) != 42)
	{
		return 8;
	}

	std::puts("phase: stop workers");
	InterlockedExchange(&g_stopWorkers, 1);
	if (WaitForMultipleObjects(workerCount, workers, TRUE, 10000) != WAIT_OBJECT_0)
	{
		std::puts("Worker threads did not stop.");
		return 9;
	}
	for (DWORD i = 0; i < workerCount; ++i)
	{
		CloseHandle(workers[i]);
	}

	if (InterlockedCompareExchange(&g_invalidResults, 0, 0) != 0)
	{
		std::puts("A worker observed an invalid hook result.");
		return 10;
	}

	std::puts("phase: uninitialize");
	if (!CheckStatus("MH_Uninitialize", MH_Uninitialize(), MH_OK))
	{
		return 11;
	}

	std::puts("MinHook smoke test passed.");
	return 0;
}
