#pragma once
#include <compat/Error.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace compat
{
// Configure/seal before notification registration. Loader callbacks publish only
// immutable ranges and scalar states into preallocated, never-reused records.
// Native records also matter: they shadow old legacy ranges after address reuse.
class EarlyModules
{
public:
    struct Match { Error error{}; uint32_t domain{}; uintptr_t base{}; size_t size{}; };
    explicit EarlyModules(size_t capacity);
    void AddRoot(std::wstring directory,uint32_t domain);
    void Seal();
    bool sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }
    Error Loaded(uintptr_t base,size_t size,std::wstring_view path) noexcept;
    Error Unloaded(uintptr_t base) noexcept;
    Match Find(uintptr_t instruction) const noexcept;
private:
    struct Root { std::wstring directory; uint32_t domain; };
    struct Record { std::atomic<unsigned> state{}; uintptr_t base{}; size_t size{}; uint32_t domain{}; };
    static bool Prefix(std::wstring_view value,std::wstring_view prefix) noexcept;
    Error Fail(Error error) noexcept;
    std::vector<Root> roots_;
    std::unique_ptr<Record[]> records_;
    size_t capacity_;
    std::atomic<size_t> count_{};
    std::atomic<bool> sealed_{};
    std::atomic<Error> failure_{Error::None};
};
#ifdef _WIN32
// Control-plane lifetime: destroy only after all plugin loads have quiesced.
// No loader notification calls OS APIs, allocates, locks, or releases ownership.
class ModuleNotifications
{
public:
    explicit ModuleNotifications(EarlyModules& modules);
    ~ModuleNotifications();
    ModuleNotifications(const ModuleNotifications&)=delete;
    ModuleNotifications& operator=(const ModuleNotifications&)=delete;
private:
    void* cookie_{};
    void* unregister_{};
};
#endif
}
