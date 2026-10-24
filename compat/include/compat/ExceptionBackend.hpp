#pragma once
#ifdef _WIN32
#include <compat/ViewArena.hpp>
#include <array>

namespace compat
{
struct FaultDiagnostic
{
    Error error{};
    uintptr_t instruction{}, address{};
    std::array<std::byte, 15> code{};
    uint8_t code_size{};
    bool write{};
};
// Exactly one installed backend. Construction/destruction require quiescence;
// remove only after all guest callers are drained. Other exceptions are untouched.
// A failed owned access is recorded and continues normal exception dispatch.
class ExceptionBackend
{
public:
    explicit ExceptionBackend(ViewArena& arena);
    ~ExceptionBackend();
    ExceptionBackend(const ExceptionBackend&) = delete;
    ExceptionBackend& operator=(const ExceptionBackend&) = delete;
    bool Pop(FaultDiagnostic& diagnostic) noexcept;
    uint64_t handled() const noexcept { return handled_.load(std::memory_order_relaxed); }
    uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    long Handle(void* pointers) noexcept;
private:
    struct Cell { std::atomic<unsigned> state{}; FaultDiagnostic diagnostic; };
    void Record(const FaultDiagnostic& diagnostic) noexcept;
    long HandleOwned(void* pointers) noexcept;
    ViewArena& arena_;
    void* handler_{};
    std::array<Cell, 64> diagnostics_{};
    std::atomic<size_t> cursor_{};
    std::atomic<uint64_t> handled_{}, dropped_{};
};
}
#endif
