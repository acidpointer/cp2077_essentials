#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/Callback.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#include <compat/Profile.hpp>

#pragma clang diagnostic push
// Deliberate SDK layout assertions only.
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#if COMPAT_OLD_LAYOUT
#define OFFSET(field) field.guest_offset
#define SIZE(type) type.guest_size
#else
#define OFFSET(field) field.native_offset
#define SIZE(type) type.native_size
#endif
static_assert(sizeof(RED4ext::CStackFrame) == SIZE(compat::profile::Types[0]));
static_assert(sizeof(RED4ext::CClassStaticFunction) == SIZE(compat::profile::Types[1]));
static_assert(sizeof(RED4ext::CClass) == SIZE(compat::profile::Types[2]));
static_assert(sizeof(RED4ext::CRTTISystem) == SIZE(compat::profile::Types[12]));
static_assert(sizeof(RED4ext::rtti::IType) == SIZE(compat::profile::Types[13]));
static_assert(sizeof(RED4ext::CProperty) == SIZE(compat::profile::Types[8]));
static_assert(offsetof(RED4ext::CProperty,type) == OFFSET(compat::profile::PropertyFields[0]));
static_assert(offsetof(RED4ext::CProperty,name) == OFFSET(compat::profile::PropertyFields[1]));
static_assert(offsetof(RED4ext::CProperty,group) == OFFSET(compat::profile::PropertyFields[2]));
static_assert(offsetof(RED4ext::CProperty,parent) == OFFSET(compat::profile::PropertyFields[3]));
static_assert(offsetof(RED4ext::CProperty,valueOffset) == OFFSET(compat::profile::PropertyFields[4]));
static_assert(offsetof(RED4ext::CProperty,flags) == OFFSET(compat::profile::PropertyFields[6]));
#define FRAME(name, index) \
    static_assert(offsetof(RED4ext::CStackFrame, name) == OFFSET(compat::profile::StackFrameFields[index])); \
    static_assert(sizeof(((RED4ext::CStackFrame*)nullptr)->name) == compat::profile::StackFrameFields[index].width)
FRAME(code, 0); FRAME(func, 1); FRAME(localVars, 2); FRAME(params, 3);
FRAME(unk20, 4); FRAME(unk28, 5); FRAME(data, 6); FRAME(dataType, 7);
FRAME(context, 8); FRAME(parent, 9); FRAME(unk50, 10); FRAME(unk58, 12);
FRAME(paramFlags, 13); FRAME(currentParam, 14); FRAME(useDirectData, 15);
#if COMPAT_OLD_LAYOUT
static_assert(sizeof(RED4ext::Callback<void (*)()>) == 0x28);
#else
static_assert(sizeof(RED4ext::Callback<void (*)()>) == 0x48);
static_assert(offsetof(RED4ext::CStackFrame, isRoot) == 0x64);
static_assert(offsetof(RED4ext::CStackFrame, unk98) == 0x98);
#endif
#pragma clang diagnostic pop
