#pragma once
// Private ABI declarations verified against SDK f9ad533. Pointer/reference
// parameter types share their declarations with the native SDK; no by-value
// engine container or native callback is substituted for an old layout.
#include <RED4ext/RTTISystem.hpp>

namespace compat::legacy
{
using Callback = RED4ext::Callback<void(*)(),32>;
static_assert(sizeof(Callback)==0x28 && offsetof(Callback,handler)==0x20);
using namespace RED4ext;
struct RttiSystem
{
    virtual rtti::IType* GetType(CName) = 0;
    virtual rtti::IType* GetTypeByAsyncId(uint32_t) = 0;
    virtual CClass* GetClass(CName) = 0;
    virtual CEnum* GetEnum(CName) = 0;
    virtual CBitfield* GetBitfield(CName) = 0;
    virtual void sub_28() = 0;
    virtual CGlobalFunction* GetFunction(CName) = 0;
    virtual void sub_38() = 0;
    virtual void GetNativeTypes(DynArray<rtti::IType*>&) = 0;
    virtual void GetGlobalFunctions(DynArray<CBaseFunction*>&) = 0;
    virtual void sub_50() = 0;
    virtual void GetClassFunctions(DynArray<CBaseFunction*>&) = 0;
    virtual void GetEnums(DynArray<CEnum*>&,bool) = 0;
    virtual void GetBitfields(DynArray<CBitfield*>&,bool) = 0;
    virtual void GetClasses(CClass*,DynArray<CClass*>&,bool(*)(CClass*),bool) = 0;
    virtual void GetDerivedClasses(CClass*,DynArray<CClass*>&) = 0;
    virtual void RegisterType(rtti::IType*,uint32_t) = 0;
    virtual void sub_88() = 0;
    virtual void sub_90() = 0;
    virtual void UnregisterType(rtti::IType*) = 0;
    virtual void RegisterFunction(CGlobalFunction*) = 0;
    virtual void UnregisterFunction(CGlobalFunction*) = 0;
    virtual void sub_B0() = 0;
    virtual void sub_B8() = 0;
    virtual void AddRegisterCallback(Callback) = 0;
    virtual void AddPostRegisterCallback(Callback) = 0;
    virtual void sub_D0() = 0;
    virtual void sub_D8() = 0;
    virtual void CreateScriptedClass(CName,CClass::Flags,CClass*) = 0;
    virtual void CreateScriptedEnum(CName,int8_t,DynArray<uint64_t>&) = 0;
    virtual void CreateScriptedBitfield(CName,DynArray<uint64_t>&) = 0;
    virtual void InitializeScriptRuntime() = 0;
    virtual void RegisterScriptName(CName,CName) = 0;
    virtual CClass* GetClassByScriptName(CName) = 0;
    virtual CEnum* GetEnumByScriptName(CName) = 0;
    virtual CName ConvertNativeToScriptName(CName) = 0;
    virtual CName ConvertScriptToNativeName(CName) = 0;
    virtual CString* GetStringConst(uint32_t) = 0;
    virtual void SetStringTable(DynArray<CString>&) = 0;
    virtual ~RttiSystem() = default; // 138, native moved to 158 in 2.5.
};
struct Type
{
    virtual ~Type() = default;
    virtual CName GetName() const = 0;
    virtual uint32_t GetSize() const = 0;
    virtual uint32_t GetAlignment() const = 0;
    virtual rtti::ERTTIType GetType() const = 0;
    virtual CString GetTypeName() const = 0;
    virtual CName GetComputedName() const = 0;
    virtual void Construct(void*) const = 0;
    virtual void Destruct(void*) const = 0;
    virtual bool IsEqual(const void*,const void*,uint32_t) = 0;
    virtual void Assign(void*,const void*) const = 0;
    virtual void Move(void*,void*) const = 0;
    virtual bool Unserialize(BaseStream*,void*,int64_t) const = 0;
    virtual bool ToString(const void*,CString&) const = 0;
    virtual bool FromString(void*,const CString&) const = 0;
    virtual bool sub_78() = 0;
    virtual bool sub_80(int64_t,void*) = 0;
    virtual bool sub_88(int64_t,void*) = 0;
    virtual bool sub_90(int64_t,void*,CString&,int64_t) = 0;
    virtual bool sub_98(int64_t,void*,CString&,int64_t,bool) = 0;
    virtual bool sub_A0(int64_t,CString&,bool) = 0;
    virtual bool sub_A8() = 0;
    virtual void sub_B0(int64_t,int64_t) = 0;
    virtual Memory::IAllocator* GetAllocator() const = 0;
    // CClass extends the common IType table with these six slots.
    virtual void sub_C0() = 0;
    virtual uint32_t GetMaxAlignment() const = 0;
    virtual bool sub_D0() const = 0;
    virtual void ConstructCls(void*) const = 0;
    virtual void DestructCls(void*) const = 0;
    virtual void* AllocMemory() const = 0;
};
}
