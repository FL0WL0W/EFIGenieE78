#include <cstddef>
#include <cstdint>

namespace
{
	constexpr std::uint32_t ExternalInterruptEnable = 0x00008000U;

	std::uint32_t LockHeap()
	{
		std::uint32_t machineState;
		asm volatile(
			"mfmsr %0\n\t"
			"wrteei 0\n\t"
			"isync"
			: "=r"(machineState)
			:
			: "memory");
		return machineState;
	}

	void UnlockHeap(const std::uint32_t machineState)
	{
		if ((machineState & ExternalInterruptEnable) != 0U)
			asm volatile("wrteei 1\n\tisync" ::: "memory");
	}
}

// This single-thread newlib build does not call __malloc_lock/__malloc_unlock.
// Linker wrapping its reentrant allocator entry points protects every user of
// the process heap, including C++ new/delete and standard-library containers.
// This is required because application code and peripheral ISR callbacks both
// allocate memory.
extern "C" void* __real__malloc_r(void* reentrancy, std::size_t size);
extern "C" void __real__free_r(void* reentrancy, void* allocation);

extern "C" void* __wrap__malloc_r(void* const reentrancy, const std::size_t size)
{
	const std::uint32_t machineState = LockHeap();
	void* const allocation = __real__malloc_r(reentrancy, size);
	UnlockHeap(machineState);
	return allocation;
}

extern "C" void __wrap__free_r(void* const reentrancy, void* const allocation)
{
	const std::uint32_t machineState = LockHeap();
	__real__free_r(reentrancy, allocation);
	UnlockHeap(machineState);
}
