#include <Windows.h>
#include <sol/sol.hpp>
#include "common/LuaLock.h"
#include "common/SandboxIO.h"
#include <cassert>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "Check failed: " << #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; std::exit(1); } } while (0)
#include "lsqlite3/lsqlite3.h"

// Only engine-facing calls/storage are replaced. The production continuation
// method and its real sol2/LuaJIT objects are compiled below, unchanged.
namespace RED4ext {
struct CBaseFunction {
    struct { bool isStatic = true; } flags;
    struct { const char* ToString() const { return "SettingsMainGameController::PopulateCategorySettingsOptions"; } } fullName;
};
struct CClassFunction; struct CClass; struct IScriptable;
struct CStackFrame { char* code{}; uint8_t currentParam{}; };
struct CStackType { void* type{}; void* value{}; };
struct CScriptStack {};
}
namespace TiltedPhoques {
template<class T> using Vector = std::vector<T>;
template<class K,class V> using Map = std::unordered_map<K,V>;
}
struct Scripting
{
    CETLua::Lockable<sol::state> State;
    auto GetLockedState() { return State.Lock(); }
    int Collections = 0;
    void CollectGarbage() { ++Collections; }
    static void ToRED(sol::object object, RED4ext::CStackType& target) {
        if (!object.is<int>()) throw sol::error("invalid test return conversion");
        *static_cast<int*>(target.value) = object.as<int>();
    }
};
#include "scripting/FunctionOverride.h"
struct Access : FunctionOverride
{
    using FunctionOverride::Invocation;
    using FunctionOverride::InvocationScope;
    using FunctionOverride::WrapNextOverride;
    using FunctionOverride::ExecuteChain;
};
// Engine trampolines/global ownership are stand-ins, not production hooks.
static int originalCalls = 0;
static bool RealRunPureScriptFunction(RED4ext::CBaseFunction*, RED4ext::CScriptStack*, RED4ext::CStackFrame*) {
    ++originalCalls; return true;
}
static bool CallScriptFunction(RED4ext::CBaseFunction*, RED4ext::IScriptable*, RED4ext::CStackFrame*, void*, void*) {
    ++originalCalls; return true;
}
struct TestOverrideOwner { Scripting* m_pScripting; };
static TestOverrideOwner* s_pOverride;
struct RTTIHelper
{
    static RTTIHelper& Get() { static RTTIHelper helper; return helper; }
    sol::variadic_results ExecuteFunction(RED4ext::CBaseFunction*, RED4ext::IScriptable*, sol::variadic_args args, int, std::string&)
    {
        ++originalCalls;
        sol::variadic_results result;
        for (auto arg : args) result.emplace_back(arg);
        return result;
    }
};
template<class F> sol::function MakeSolFunction(sol::state& lua, F function)
{
    lua["___func_temp_holder_"] = function;
    sol::function result = lua["___func_temp_holder_"];
    lua["___func_temp_holder_"] = sol::nil;
    return result;
}
#include "continuation.inc"
#include "execute-chain.inc"

static void continuations(Scripting& scripting, sol::state& lua)
{
    RED4ext::CBaseFunction native;
    auto chain = std::make_shared<FunctionOverride::CallChain>();
    chain->Trampoline = &native; chain->pScripting = &scripting;
    for (const char* body : {
        "return function(x,next) saved=next; return next(x+1)*2 end",
        "return function(x,next) return next(x*3)+5 end"})
    {
        auto context = std::make_shared<FunctionOverride::Context>();
        context->ScriptFunction = lua.script(body).get<sol::protected_function>();
        chain->Overrides.push_back(context);
    }
    auto invocation = std::make_shared<Access::Invocation>();
    invocation->Chain = chain; invocation->Function = &native;
    invocation->LuaContext = sol::object{};
    invocation->Args.push_back(sol::make_object(lua,3));
    sol::protected_function escaped;
    {
        Access::InvocationScope scope{invocation};
        sol::protected_function wrapped = Access::WrapNextOverride(invocation,0,lua);
        auto result = wrapped(3);
        assert(result.valid() && result.get<int>() == 25);
        escaped = lua["saved"];
    }
    assert(!invocation->Alive && !invocation->Chain && invocation->Args.empty());
    lua.collect_garbage();
    auto stale = escaped(1);
    assert(!stale.valid());
    std::cerr << "Expired-next diagnostic: " << stale.get<sol::error>().what() << "\n";
    assert(std::string(stale.get<sol::error>().what()).find("expired") != std::string::npos);
    chain->Overrides[0]->ScriptFunction = lua.script("return function() error('bad%s%n%%') end").get<sol::protected_function>();
    auto failure = std::make_shared<Access::Invocation>();
    failure->Chain = chain; failure->Function = &native;
    failure->Args.push_back(sol::make_object(lua,1));
    {
        Access::InvocationScope scope{failure};
        sol::protected_function wrapped = Access::WrapNextOverride(failure,0,lua);
        auto result = wrapped(1);
        assert(!result.valid());
        assert(std::string(result.get<sol::error>().what()).find("bad%s%n%%") != std::string::npos);
    }
}
static void dispatch(Scripting& scripting, sol::state& lua)
{
    std::ostringstream modLog, fallbackLog;
    auto modLogger = std::make_shared<spdlog::logger>("probe-mod", std::make_shared<spdlog::sinks::ostream_sink_mt>(modLog));
    auto fallback = std::make_shared<spdlog::logger>("scripting", std::make_shared<spdlog::sinks::ostream_sink_mt>(fallbackLog));
    spdlog::register_logger(fallback);
    sol::environment env(lua, sol::create);
    env["__logger"] = modLogger;
    auto context = [&](const char* body, bool withEnvironment = true) {
        auto call = std::make_shared<FunctionOverride::Context>();
        call->ScriptFunction = lua.script(body).get<sol::protected_function>();
        if (withEnvironment) call->Environment = env;
        return call;
    };
    RED4ext::CBaseFunction native;
    FunctionOverride::CallChain chain{};
    chain.Trampoline = &native;
    chain.pScripting = &scripting;
    chain.CollectGarbage = true;
    chain.Before.push_back(context("return function() beforeCount=beforeCount+1 end"));
    chain.After.push_back(context("return function() afterCount=afterCount+1 end"));
    lua["beforeCount"] = 0; lua["afterCount"] = 0;
    std::vector<sol::object> args;
    int returned = -7, output = -8;
    RED4ext::CStackType result{nullptr, &returned};
    std::vector<RED4ext::CStackType> out{{nullptr, &output}};
    TestOverrideOwner owner{&scripting};
    s_pOverride = &owner;
    auto run = [&] {
        args = {sol::make_object(lua, 3)};
        originalCalls = 0;
        return Access::ExecuteChain(chain, nullptr, &args, &result, &out, nullptr, nullptr, nullptr, 0);
    };
    chain.Overrides = {context("return function(x,next) return next(x+1),9 end")};
    assert(run());
    assert(returned == 4 && output == 9 && originalCalls == 1 && args.empty());
    assert(lua["beforeCount"].get<int>() == 1 && lua["afterCount"].get<int>() == 1);
    const int collections = scripting.Collections;
    chain.Overrides = {context("return function(x,next) savedFailureNext=next; error('bad%s%n%%{payload}') end")};
    returned = -7; output = -8;
    assert(!run());
    assert(returned == -7 && output == -8 && originalCalls == 0 && args.empty());
    assert(scripting.Collections == collections + 1);
    assert(lua["beforeCount"].get<int>() == 2 && lua["afterCount"].get<int>() == 2);
    assert(modLog.str().find("bad%s%n%%{payload}") != std::string::npos);
    assert(modLog.str().find("PopulateCategorySettingsOptions") != std::string::npos);
    assert(fallbackLog.str().empty());
    auto expired = lua["savedFailureNext"].get<sol::protected_function>()(1);
    assert(!expired.valid() && std::string(expired.get<sol::error>().what()).find("expired") != std::string::npos);

    // Invalid environments use the shared scripting log. Failure after next()
    // must not retry the original engine call or convert missing return values.
    chain.Overrides = {context("return function(x,next) next(x); error('after-next') end", false)};
    assert(!run());
    assert(originalCalls == 1 && args.empty() && returned == -7 && output == -8);
    assert(fallbackLog.str().find("after-next") != std::string::npos);

    // Nested failures escape through the full dispatcher; handled errors do not
    // become failure just because a lower continuation threw an exception.
    chain.Overrides = {context("return function() error('nested-failure') end"),
                       context("return function(x,next) return next(x) end")};
    assert(!run());
    assert(originalCalls == 0 && modLog.str().find("nested-failure") != std::string::npos);
    modLog.str(""); modLog.clear();
    chain.Overrides.back() = context("return function(x,next) local ok=pcall(next,x); assert(not ok); return 11,12 end");
    assert(run());
    assert(returned == 11 && output == 12 && modLog.str().empty() && args.empty());
    spdlog::drop("scripting");
    s_pOverride = nullptr;
}

static void sqlite(sol::state& lua)
{
    lua.require("sqlite3", luaopen_lsqlite3);
    auto result = lua.safe_script(R"(
      local sql = sqlite3
      local db = assert(sql.open_memory())
      assert(db:exec('create table test(value text);') == sql.OK)
      local statement = assert(db:prepare('insert into test values (?)'))
      assert(statement:bind_values('Привіт 世界') == sql.OK)
      assert(statement:step() == sql.DONE)
      assert(statement:finalize() == sql.OK)
      local held = assert(db:prepare('select * from test'))
      assert(held:step() == sql.ROW)
      assert(held:get_value(0) == 'Привіт 世界')
      assert(db:close() == sql.OK)
      held = nil; db = nil; collectgarbage('collect')
      -- Closing a connection while an online backup remains live must not
      -- lose the native connection or break backup cleanup.
      local source = assert(sql.open_memory())
      local target = assert(sql.open_memory())
      source:exec('create table t(n); insert into t values(1)')
      local backup = assert(sql.backup_init(target,'main',source,'main'))
      assert(source:close() == sql.OK)
      assert(target:close() == sql.OK)
      backup:finish()
      backup = nil; source = nil; target = nil; collectgarbage('collect')
    )", sol::script_pass_on_error);
    if (!result.valid()) throw std::runtime_error(result.get<sol::error>().what());
}
static void unicodeFiles(sol::state& lua)
{
    const auto directory = std::filesystem::temp_directory_path() / (L"cet-Привіт世界-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    auto utf8 = directory.u8string();
    lua["testDirectory"] = std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());
    auto result = lua.safe_script(R"(
      local path = testDirectory .. '/тест.lua'
      local file = assert(io.open(path,'wb'))
      file:write('return {value=42}')
      file:close()
      assert(assert(loadfile(path))().value == 42)
      local renamed = testDirectory .. '/новий.lua'
      assert(os.rename(path,renamed))
      assert(os.remove(renamed))
      assert(io.open(path,'invalid-mode') == nil)
    )",sol::script_pass_on_error);
    if (!result.valid()) std::cerr << result.get<sol::error>().what() << '\n';
    assert(result.valid());
    const auto currentDirectory = std::filesystem::current_path();
    auto factory = lua.script(CETSandboxIOFactory).get<sol::protected_function>();
    auto first = factory(lua["io"].get<sol::table>(), [base = lua["testDirectory"].get<std::string>()](const std::string& name,bool) { return base + "/a-" + name; });
    auto second = factory(lua["io"].get<sol::table>(), [base = lua["testDirectory"].get<std::string>()](const std::string& name,bool) { return base + "/b-" + name; });
    assert(first.valid() && second.valid());
    lua["firstIO"] = first.get<sol::table>(); lua["secondIO"] = second.get<sol::table>();
    auto streams = lua.safe_script(R"(
      firstIO.output('log.txt'); secondIO.output('log.txt')
      firstIO.write('first'); secondIO.write('second')
      firstIO.close(); secondIO.close()
      firstIO.input('log.txt'); secondIO.input('log.txt')
      assert(firstIO.read('*a') == 'first')
      assert(secondIO.read('*a') == 'second')
      firstIO.close(firstIO.input()); secondIO.close(secondIO.input())
    )",sol::script_pass_on_error);
    assert(streams.valid());
    assert(std::filesystem::current_path() == currentDirectory);
    std::filesystem::remove_all(directory);
}

int main()
{
    Scripting scripting;
    auto locked = scripting.GetLockedState();
    auto& lua = locked.Get();
    lua.open_libraries(sol::lib::base,sol::lib::package,sol::lib::io,sol::lib::os,sol::lib::math,sol::lib::table,sol::lib::string,sol::lib::jit,sol::lib::ffi);
    continuations(scripting,lua);
    dispatch(scripting,lua);
    for (bool enabled : {false,true})
    {
        lua.safe_script(enabled ? "jit.on()" : "jit.off()", sol::script_pass_on_error);
        auto result = lua.safe_script("local n=0; for i=1,100000 do n=n+i end; collectgarbage('collect'); assert(n==5000050000)", sol::script_pass_on_error);
        assert(result.valid());
    }
    auto ffi = lua.safe_script("local ffi=require('ffi'); ffi.cdef 'unsigned long GetCurrentProcessId(void);'; assert(ffi.C.GetCurrentProcessId()>0)", sol::script_pass_on_error);
    if (!ffi.valid()) std::cerr << "FFI smoke diagnostic: " << ffi.get<sol::error>().what() << "\n";
    assert(ffi.valid());
    sqlite(lua);
    unicodeFiles(lua);
    std::cout << "CET full override dispatch, continuation lifetime, errors, LuaJIT, FFI and SQLite checks passed\n";
}
