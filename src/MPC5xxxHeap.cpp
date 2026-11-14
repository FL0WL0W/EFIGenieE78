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

// Picolibc's single-thread allocator is shared by main and peripheral callbacks.
// Wrap every allocation entry point so nested ISR allocations cannot corrupt it.
extern "C" void* __real_malloc(std::size_t size);
extern "C" void __real_free(void* allocation);
extern "C" void* __real_calloc(std::size_t count, std::size_t size);
extern "C" void* __real_realloc(void* allocation, std::size_t size);

extern "C" void* __wrap_malloc(const std::size_t size)
{
    const std::uint32_t machineState = LockHeap();
    void* const allocation = __real_malloc(size);
    UnlockHeap(machineState);
    return allocation;
}

extern "C" void __wrap_free(void* const allocation)
{
    const std::uint32_t machineState = LockHeap();
    __real_free(allocation);
    UnlockHeap(machineState);
}

extern "C" void* __wrap_calloc(const std::size_t count, const std::size_t size)
{
    const std::uint32_t machineState = LockHeap();
    void* const allocation = __real_calloc(count, size);
    UnlockHeap(machineState);
    return allocation;
}

extern "C" void* __wrap_realloc(void* const allocation, const std::size_t size)
{
    const std::uint32_t machineState = LockHeap();
    void* const resized = __real_realloc(allocation, size);
    UnlockHeap(machineState);
    return resized;
}
