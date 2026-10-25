// Isolated conversion/control-flow probe. No game or mod DLL is loaded.
// The production ClassConverter::ToRED body is extracted unchanged by run.py.
#include <sol/sol.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

namespace RED4ext {
namespace rtti {
enum class ERTTIType { Class };
struct IType {
    const char* name;
    IType* parent{};
    ERTTIType GetType() const { return ERTTIType::Class; }
    bool IsA(IType* target) const {
        for (auto* current = this; current; current = current->parent)
            if (current == target) return true;
        return false;
    }
};
}
using CClass = rtti::IType;
struct CStackType { rtti::IType* type{}; void* value{}; };
}
namespace TiltedPhoques { struct Allocator {}; }
struct ClassReference {
    RED4ext::rtti::IType* type;
    void* instance;
    auto* GetType() const { return type; }
    void* GetHandle() const { return instance; }
};
struct ClassConverter {
#include "converter.inc"
};

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
    require(argc == 4, "usage: probe native-settings-dir probe.lua strict|ancestry|production");
    const bool ancestry = std::string(argv[3]) == "ancestry";
    const bool expectFixed = ancestry || std::string(argv[3]) == "production";
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string,
                      sol::lib::math, sol::lib::package, sol::lib::debug);
    RED4ext::rtti::IType widget{"inkWidgetReference"};
    RED4ext::rtti::IType leaf{"inkLeafWidgetReference", &widget};
    RED4ext::rtti::IType text{"inkTextWidgetReference", &leaf};
    RED4ext::rtti::IType compound{"inkCompoundWidgetReference", &widget};
    RED4ext::rtti::IType unrelated{"UnrelatedStruct"};
    int storage{};
    auto convert = [&](sol::object value, RED4ext::rtti::IType* target) {
        if (!ancestry) return ClassConverter{}.ToRED(value, target, nullptr);
        // Candidate semantics only: production correction must use real CClass
        // kind/ancestry checks. All descriptors in this model are class types.
        RED4ext::CStackType result{target, nullptr};
        if (value.is<ClassReference>()) {
            auto* source = value.as<ClassReference*>();
            if (source->GetType()->IsA(target)) result.value = source->GetHandle();
        }
        return result;
    };
    lua.new_usertype<ClassReference>("ProbeWidgetReference", sol::no_constructor,
        "SetVisible", [&](ClassReference& self, bool) {
            if (!convert(sol::make_object(lua, &self), &widget).value)
                throw sol::error("Function 'SetVisible' parameter 1 must be inkWidgetReference.");
        });
    ClassReference description{&text, &storage};
    lua["descriptionReference"] = &description;
    for (auto* source : {&widget, &text, &compound, &unrelated}) {
        ClassReference value{source, &storage};
        const bool accepted = convert(sol::make_object(lua, &value), &widget).value;
        std::cout << argv[3] << ": " << source->name << " -> " << widget.name
                  << " accepted=" << accepted << '\n';
        require(accepted == (source == &widget || (expectFixed && source->IsA(&widget))), "conversion matrix mismatch");
    }
    ClassReference base{&widget, &storage};
    require(!convert(sol::make_object(lua, &base), &text).value, "base-to-derived must remain rejected");
    lua["inputDirectory"] = argv[1];
    lua["ancestryMode"] = expectFixed;
    auto result = lua.safe_script_file(argv[2], sol::script_pass_on_error);
    if (!result.valid()) {
        std::cerr << result.get<sol::error>().what() << '\n';
        return 1;
    }
}
