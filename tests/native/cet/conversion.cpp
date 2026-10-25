// Production conversion method and SDK ancestry implementation, real Windows
// sol2/LuaJIT; only engine RTTI descriptors and ClassReference storage are fake.
#include <sol/sol.hpp>
#include <cstdlib>
#include <iostream>

namespace RED4ext {
namespace rtti {
enum class ERTTIType { Class, Simple };
struct IType {
    virtual ERTTIType GetType() const { return ERTTIType::Simple; }
    virtual ~IType() = default;
};
}
struct CClass : rtti::IType {
    CClass* parent{};
    explicit CClass(CClass* base = nullptr) : parent(base) {}
    rtti::ERTTIType GetType() const override { return rtti::ERTTIType::Class; }
    bool IsA(const rtti::IType* type) const;
};
struct CStackType { rtti::IType* type{}; void* value{}; };
}
#define RED4EXT_INLINE inline
#include "class-is-a.inc"
namespace TiltedPhoques { struct Allocator {}; }
struct ClassReference {
    RED4ext::rtti::IType* type;
    void* data;
    auto* GetType() const { return type; }
    void* GetHandle() const { return data; }
};
struct ClassConverter {
#include "class-converter.inc"
};

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(42); }
}

int main() {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    lua.new_usertype<ClassReference>("ClassReference", sol::no_constructor);
    RED4ext::CClass widget, leaf(&widget), text(&leaf), compound(&widget), unrelated;
    RED4ext::rtti::IType scalar;
    int storage = 123;
    auto check = [&](RED4ext::rtti::IType* from, RED4ext::rtti::IType* to, bool accepted, const char* message) {
        ClassReference source{from, &storage};
        auto result = ClassConverter{}.ToRED(sol::make_object(lua, &source), to, nullptr);
        require(result.type == to, "destination type is preserved");
        require(result.value == (accepted ? &storage : nullptr), message);
        require(storage == 123, "conversion borrows storage without modifying it");
    };
    check(&widget, &widget, true, "exact class");
    check(&text, &widget, true, "inkTextRef -> inkWidgetRef ancestor conversion");
    check(&text, &leaf, true, "immediate ancestor conversion");
    check(&compound, &widget, true, "inkCompoundRef -> inkWidgetRef ancestor conversion");
    check(&widget, &text, false, "base-to-derived remains rejected");
    check(&text, &compound, false, "sibling conversion remains rejected");
    check(&unrelated, &widget, false, "unrelated same-size struct remains rejected");
    check(nullptr, &widget, false, "null source descriptor");
    check(&text, nullptr, false, "null destination descriptor");
    check(&scalar, &widget, false, "non-class source descriptor");
    check(&text, &scalar, false, "non-class destination descriptor");
    check(&scalar, &scalar, false, "matching non-class descriptors cannot masquerade as classes");
    require(!ClassConverter{}.ToRED(sol::make_object(lua, 7), &widget, nullptr).value, "wrong Lua value");
    require(!ClassConverter{}.ToRED(sol::make_object(lua, sol::nil), &widget, nullptr).value, "nil Lua value");
    ClassReference empty{&text, nullptr};
    require(!ClassConverter{}.ToRED(sol::make_object(lua, &empty), &widget, nullptr).value, "empty native storage");
    std::cout << "CET native struct conversion: ancestry, rejection and borrowed-storage checks passed\n";
}
