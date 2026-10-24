#define RED4EXT_STATIC_LIB
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#include <RED4ext/Callback.hpp>
#include <RED4ext/Memory/SharedPtr.hpp>
#include <cstdio>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

#define TYPE(T) std::printf("TYPE %s %zu %zu\n", #T, sizeof(RED4ext::T), alignof(RED4ext::T))
#define FIELD(T,F) std::printf("FIELD %s.%s %zu %zu\n", #T, #F, offsetof(RED4ext::T,F), sizeof(((RED4ext::T*)0)->F))
int main()
{
    TYPE(CBaseFunction); TYPE(CGlobalFunction); TYPE(CClassFunction); TYPE(CClassStaticFunction);
    TYPE(CClass); TYPE(CProperty); TYPE(CStack); TYPE(CStackType); TYPE(CStackFrame); TYPE(RefCnt);
    using Callback = RED4ext::Callback<void (*)()>;
    std::printf("TYPE Callback %zu %zu\n", sizeof(Callback), alignof(Callback));
    std::printf("FIELD Callback.handler %zu %zu\n", offsetof(Callback,handler), sizeof(void*));
    FIELD(CBaseFunction,fullName); FIELD(CBaseFunction,returnType); FIELD(CBaseFunction,params);
    FIELD(CBaseFunction,bytecode); FIELD(CBaseFunction,flags); FIELD(CBaseFunction,unkAC);
    FIELD(CClassFunction,parent); FIELD(CClassFunction,regIndex); FIELD(CGlobalFunction,regIndex);
    FIELD(CClass,parent); FIELD(CClass,props); FIELD(CClass,funcs); FIELD(CClass,staticFuncs);
    FIELD(CClass,funcsByName); FIELD(CClass,propsByName);
    FIELD(CProperty,type); FIELD(CProperty,parent); FIELD(CProperty,valueOffset); FIELD(CProperty,flags);
    FIELD(CStackFrame,code); FIELD(CStackFrame,func); FIELD(CStackFrame,data); FIELD(CStackFrame,dataType);
    FIELD(CStackFrame,parent); FIELD(CStackFrame,currentParam); FIELD(CStackFrame,useDirectData);
    FIELD(CStack,args); FIELD(CStack,result); FIELD(CStackType,type); FIELD(CStackType,value);
    FIELD(RefCnt,strongRefs); FIELD(RefCnt,weakRefs);
}

#pragma clang diagnostic pop
