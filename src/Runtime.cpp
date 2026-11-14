#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <__verbose_abort>

extern "C" int write(int file, const void* data, std::size_t length);

namespace
{
    int ConsolePutchar(char value, FILE*)
    {
        return write(1, &value, 1) == 1 ? 0 : _FDEV_ERR;
    }

    int ConsoleGetchar(FILE*)
    {
        return _FDEV_EOF;
    }

    FILE console = FDEV_SETUP_STREAM(ConsolePutchar, ConsoleGetchar, nullptr, _FDEV_SETUP_RW);

    [[noreturn]] void Halt()
    {
        asm volatile("wrteei 0" ::: "memory");
        for (;;)
            asm volatile("" ::: "memory");
    }
}

// Route Picolibc stdio through the existing ISO-TP console write hook.
extern "C"
{
    FILE* const stdin = &console;
    FILE* const stdout = &console;
    FILE* const stderr = &console;

    // This bare-metal executable has no crtbegin object to supply its DSO handle.
    void* __dso_handle = &__dso_handle;
}

extern "C" [[noreturn]] void abort()
{
    Halt();
}

extern "C" [[noreturn]] void __cxa_pure_virtual()
{
    Halt();
}

// Picolibc's allocator requests space from the linker-reserved heap. The
// allocator entry points are protected from interrupts by MPC5xxxHeap.cpp.
extern "C" void* sbrk(std::ptrdiff_t increment)
{
    extern unsigned char __HEAP[], __HEAP_END[];
    static std::uintptr_t current = reinterpret_cast<std::uintptr_t>(__HEAP);
    const auto begin = reinterpret_cast<std::uintptr_t>(__HEAP);
    const auto end = reinterpret_cast<std::uintptr_t>(__HEAP_END);
    if ((increment >= 0 && static_cast<std::uintptr_t>(increment) > end - current) ||
        (increment < 0 && static_cast<std::uintptr_t>(-(increment + 1)) + 1 > current - begin))
    {
        errno = ENOMEM;
        return reinterpret_cast<void*>(-1);
    }
    const auto previous = current;
    current += increment;
    return reinterpret_cast<void*>(previous);
}

// Keep unrecoverable libc++ failures terminal.
[[noreturn]] void std::_LIBCPP_ABI_NAMESPACE::__libcpp_verbose_abort(const char*, ...) noexcept
{
    Halt();
}
