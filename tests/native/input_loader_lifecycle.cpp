// Loads the actual Windows DLL with a stub SDK; never loads the game.
#include <Windows.h>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/GameStates.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace fs = std::filesystem;
RED4ext::v1::GameState callback{};
bool rejectCallback = false;
bool RegisterState(RED4ext::v1::PluginHandle, RED4ext::EGameStateType type, RED4ext::v1::GameState* state) {
    if (type != RED4ext::EGameStateType::BaseInitialization) return false;
    if (rejectCallback) return false;
    callback = *state;
    return true;
}
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) { try {
    const std::string mode = argc > 1 ? argv[1] : "normal";
    wchar_t executable[32768];
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    Require(length && length < 32768, "executable path unavailable");
    const auto root = fs::path(std::wstring(executable, length)).parent_path().parent_path().parent_path();
    const auto dll = root / L"red4ext/plugins/input_loader/input_loader.dll";
    const HMODULE module = LoadLibraryW(dll.c_str());
    Require(module != nullptr, "LoadLibrary failed");
    using Main = bool(*)(HMODULE, RED4ext::v1::EMainReason, const RED4ext::v1::Sdk*);
    using TryAdd = bool(*)(HMODULE, const wchar_t*);
    const auto mainFn = reinterpret_cast<Main>(GetProcAddress(module, "Main"));
    const auto add = reinterpret_cast<TryAdd>(GetProcAddress(module, "TryAdd"));
    const auto remove = reinterpret_cast<TryAdd>(GetProcAddress(module, "Remove"));
    Require(mainFn && add && remove && GetProcAddress(module, "Add"), "public exports missing");
    RED4ext::v1::SemVer version{}; version.major = 2; version.minor = 5;
    RED4ext::v1::GameStates states{RegisterState};
    RED4ext::v1::Sdk sdk{}; sdk.runtime = &version; sdk.gameStates = &states;
    const auto owner = GetModuleHandleW(nullptr);
    const auto registered = (root/L"r6/registered.xml").wstring();
    const auto scanned = (root/L"r6/input/append.xml").wstring();
    if (mode == "pre-add") Require(add(owner, registered.c_str()), "pre-Main registration rejected");
    rejectCallback = mode == "callback-denied";
    if (mode == "stale-error") SetLastError(ERROR_INSUFFICIENT_BUFFER);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (mode == "log-denied") {
        file = CreateFileW((root/L"red4ext/logs/input_loader.log").c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "failed to lock log");
    }
    const bool noSdk = mode == "null-sdk" || mode == "legacy-null";
    const bool loaded = mainFn(module, RED4ext::v1::EMainReason::Load, noSdk ? nullptr : &sdk);
    const bool expected = !(noSdk || rejectCallback || mode == "empty-base" || mode == "bad-base" ||
                           mode == "multiple-roots" || mode == "scan-fail" || mode == "cache-dir-fail" ||
                           mode == "legacy-cache-fail" || mode == "custom");
    Require(loaded == expected, "unexpected Main result");
    std::printf("MAIN=%d ENTER=%d EXIT=%d\n", loaded, callback.OnEnter != nullptr, callback.OnExit != nullptr);
    if (loaded) {
        Require(callback.OnEnter && !callback.OnExit, "must register only the no-I/O entry seal");
        if (mode == "duplicate-add" || mode == "aliases") {
            Require(add(owner, scanned.c_str()), "repeat scanner registration rejected");
            Require(add(owner, L"../../r6/input/../input/append.xml"), "relative alias rejected");
            if (mode == "aliases") Require(add(owner, (root/L"r6/input/alias.XML").c_str()), "file symlink alias rejected");
        }
        if (mode == "concurrent") {
            std::atomic<bool> accepted{true}; std::vector<std::thread> threads;
            for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
                for (int n = 0; n < 3; ++n) if (!add(owner, scanned.c_str())) accepted = false;
            });
            for (auto& thread : threads) thread.join();
            Require(accepted, "concurrent registration failed");
        }
        if (mode == "cancel") {
            Require(add(owner, registered.c_str()), "registration failed");
            Require(remove(owner, registered.c_str()), "cancellation failed");
            Require(!remove(owner, registered.c_str()), "duplicate cancellation reported success");
        }
        if (mode == "immutable-base") {
            std::ofstream(root/L"r6/config/inputContexts.xml", std::ios::trunc) << "<broken>";
            Require(add(owner, registered.c_str()), "immutable baseline was re-read");
        }
        if (mode == "selector-denied") {
            file = CreateFileW((root/L"engine/config/platform/pc/input_loader.ini").c_str(), GENERIC_READ,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(file != INVALID_HANDLE_VALUE, "failed to lock selector");
            Require(!add(owner, registered.c_str()), "failed publication reported success");
        }
        Require(!add(nullptr, registered.c_str()), "null owner accepted");
        Require(!add(owner, L""), "empty path accepted");
        if (mode == "seal") {
            Require(callback.OnEnter(nullptr), "seal callback failed");
            Require(!add(owner, registered.c_str()) && !remove(owner, scanned.c_str()), "late mutation accepted");
        }
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
        Require(mainFn(module, RED4ext::v1::EMainReason::Unload, &sdk), "Unload failed");
        Require(!add(owner, registered.c_str()), "mutation after unload accepted");
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    FreeLibrary(module);
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; } }
