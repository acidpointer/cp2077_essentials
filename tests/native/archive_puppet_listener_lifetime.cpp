#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Core/Stl.hpp"
#include "App/Extensions/PuppetState/ListenerRegistry.hpp"

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(42);
    }
}
struct TestAllocator : TiltedPhoques::Allocator
{
    void* Allocate(size_t size) noexcept override { return std::malloc(size); }
    void Free(void* data) noexcept override { std::free(data); }
    size_t Size(void*) noexcept override { return 0; }
};
struct Handler
{
    inline static int destroyed = 0;
    ~Handler() { ++destroyed; }
};
using Registry = App::ListenerRegistry<Core::Map<int, std::shared_ptr<Handler>>>;
}

TiltedPhoques::Allocator* TiltedPhoques::Allocator::Get() noexcept
{
    static TestAllocator allocator;
    return &allocator;
}

int main()
{
    Registry registry;
    std::weak_ptr<Handler> weak;
    int registered = 0, unregistered = 0;
    registry.Attach(1, [&]
    {
        Require(!registry.Find(1), "construction held/published state prematurely");
        auto handler = std::make_shared<Handler>();
        weak = handler;
        return handler;
    }, [&](const auto& handler)
    {
        Require(registry.Find(1) == handler, "registration cannot reenter state readers");
        ++registered;
    });
    registry.Attach(1, []() -> std::shared_ptr<Handler> { std::abort(); }, [](const auto&) { std::abort(); });
    auto reader = registry.Find(1);
    registry.Detach(1, [&](const auto& handler)
    {
        Require(!registry.Find(1), "detach retained the map entry or held its lock");
        Require(handler && handler == weak.lock() && handler == reader, "detach lost listener ownership/identity");
        ++unregistered;
    });
    registry.Detach(1, [](const auto&) { std::abort(); });
    Require(!weak.expired() && Handler::destroyed == 0, "concurrent reader lost ownership on detach");
    reader.reset();
    Require(weak.expired() && Handler::destroyed == 1 && registered == 1 && unregistered == 1,
            "listener was not destroyed exactly once after unregister/readers");

    bool threw = false;
    try
    {
        registry.Attach(2, []() -> std::shared_ptr<Handler> { throw std::runtime_error("construction"); },
                        [](const auto&) { std::abort(); });
    }
    catch (const std::runtime_error&) { threw = true; }
    Require(threw && !registry.Find(2), "failed construction left a registry entry");

    std::promise<void> inRegister, releaseRegister, startedDetach, finishedDetach;
    auto release = releaseRegister.get_future();
    auto finished = finishedDetach.get_future();
    std::atomic<bool> registrationDone = false;
    std::thread attaching([&]
    {
        registry.Attach(3, [] { return std::make_shared<Handler>(); }, [&](const auto& handler)
        {
            Require(registry.Find(3) == handler, "state reader blocked behind native registration");
            inRegister.set_value();
            release.wait();
            registrationDone = true;
        });
    });
    inRegister.get_future().wait();
    std::thread detaching([&]
    {
        startedDetach.set_value();
        registry.Detach(3, [&](const auto& handler)
        {
            Require(registrationDone && handler && !registry.Find(3), "concurrent detach raced registration");
        });
        finishedDetach.set_value();
    });
    startedDetach.get_future().wait();
    Require(finished.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
            "detach completed before native registration finished");
    releaseRegister.set_value();
    attaching.join();
    detaching.join();
    Require(!registry.Find(3) && Handler::destroyed == 2, "concurrent lifecycle orphaned a listener");
    std::puts("PASS: real Core::Map erase lifetime, reader ownership, reentry and concurrent attach/detach");
}
