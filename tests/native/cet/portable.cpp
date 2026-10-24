#include "common/LuaLock.h"
#include "common/TaskQueue.h"
#include "common/MetadataReader.h"
#include "common/JsonConfig.h"
#include <cassert>
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

static void locks()
{
    std::optional<CETLua::Lockable<int>::Ref> reference;
    {
        CETLua::Lockable<int> owner;
        reference.emplace(owner.AsRef());
        auto first = owner.Lock();
        first.Get() = 37;
        auto recursive = reference->Lock();
        assert(recursive.Get() == 37);
    }
    auto expired = reference->Lock();
    assert(!expired.IsValid());
    bool threw = false;
    try { (void)expired.Get(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
    std::optional<CETLua::Locked<int>> old, replacement;
    {
        CETLua::Lockable<int> first, second;
        old.emplace(first.Lock());
        replacement.emplace(second.Lock());
        replacement->Get() = 91;
    }
    *old = std::move(*replacement);
    assert(old->Get() == 91);
    CETLua::Lockable<int> owner;
    std::promise<void> entered, release;
    auto released = release.get_future();
    std::thread worker([&] { auto held = owner.Lock(); entered.set_value(); released.wait(); held.Get() = 42; });
    entered.get_future().wait();
    release.set_value();
    { auto acquired = owner.AsRef().Lock(); assert(acquired.Get() == 42); }
    worker.join();
}
static void queues()
{
    CETTaskQueue queue;
    int first = 0, nested = 0;
    queue.AddTask([&] { ++first; queue.AddTask([&] { ++nested; return true; }); queue.Drain(); return first == 2; });
    queue.Drain(); assert(first == 1 && nested == 0);
    queue.Drain(); assert(first == 2 && nested == 1);
    queue.Drain(); assert(first == 2 && nested == 2);
    bool fail = true;
    int after = 0;
    queue.AddTask([&] { if (fail) throw std::runtime_error("fixture"); return true; });
    queue.AddTask([&] { ++after; return true; });
    try { queue.Drain(); assert(false); } catch (const std::runtime_error&) {}
    assert(after == 1);
    fail = false; queue.Drain(); assert(after == 1);
    queue.AddTask([&] { queue.Close(); return false; });
    queue.AddTask([] { assert(false); return true; });
    queue.Drain(); queue.AddTask([] { assert(false); return true; }); queue.Drain();
    CETTaskQueue concurrent;
    std::promise<void> entered, release;
    auto released = release.get_future();
    concurrent.AddTask([&] { entered.set_value(); released.wait(); return false; });
    std::thread worker([&] { concurrent.Drain(); });
    entered.get_future().wait(); concurrent.Close(); release.set_value(); worker.join();
    concurrent.Drain();
}
static void metadata()
{
    Metadata::CheckFileSize(Metadata::MaxDecodedSize);
    bool oversized = false;
    try { Metadata::CheckFileSize(Metadata::MaxDecodedSize + 1); }
    catch (const std::runtime_error&) { oversized = true; }
    assert(oversized);
    for (size_t size = 0; size < 8; ++size)
    {
        std::string header(size, 'x'); bool threw = false;
        try { Metadata::DecodedSize(header.data(), header.size()); } catch (const std::runtime_error&) { threw = true; }
        assert(threw);
    }
    const char valid[] = {'K','A','R','K', 16,0,0,0};
    assert(Metadata::DecodedSize(valid, sizeof(valid)) == 16);
    for (std::string input : {std::string(), std::string("\x40",1), std::string("\x80",1), std::string("\x40\x80\x80\x80\xff",5)})
    {
        std::istringstream stream(input); bool threw = false;
        try { Metadata::Length(stream); } catch (const std::runtime_error&) { threw = true; }
        assert(threw);
    }
    std::istringstream boundary(std::string("\x7f\x01",2));
    assert(Metadata::Length(boundary) == 127);
    bool threw = false;
    try { Metadata::Count(UINT32_MAX); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}
static void config()
{
    auto object = nlohmann::json::parse(R"({"bool":true,"wrong":"12","negative":-1,"too_big":18446744073709551615,"history":["one","two"]})");
    assert(Config::Value(object,"bool",false));
    assert(Config::Value(object,"wrong",12) == 12);
    assert(Config::Value(object,"negative",uint64_t(7)) == 7);
    assert(Config::Value(object,"too_big",int32_t(7)) == 7);
    assert(Config::Value(nlohmann::json::array(),"bool",true));
    assert(Config::Node(nlohmann::json(1),"font").is_object());
    auto history = Config::Value(object,"history",std::vector<std::string>{});
    assert(history.size() == 2 && history[1] == "two");
    std::map<std::string,std::map<std::string,uint64_t>> bindings;
    Config::HadInvalidValue = false;
    Config::Bindings(nlohmann::json::parse(R"({"WalkByDefault2.0":{"walk":123},"legacy.jump":99})"),
        [&](const auto& mod,const auto& id,uint64_t code) { bindings[mod][id] = code; });
    assert(!Config::HadInvalidValue);
    assert(bindings["WalkByDefault2.0"]["walk"] == 123 && bindings["legacy"]["jump"] == 99);
    Config::Bindings(nlohmann::json::parse(R"({"broken":{"key":-1,"float":0.5,"bool":true}})"),
        [&](const auto&,const auto&,uint64_t) { assert(false); });
    assert(Config::HadInvalidValue);
}
int main() { locks(); queues(); metadata(); config(); std::cout << "CET portable ownership, queue, metadata and config checks passed\n"; }
