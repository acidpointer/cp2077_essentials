#ifdef _WIN32
#include <compat/RttiAdapter.hpp>
#include <compat/CallbackBridge.hpp>
#include <compat/Profile.hpp>
#include "LegacyVirtuals.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace compat
{
namespace
{
[[noreturn]] void Unsupported() { throw Failure(Error::UnknownOperation); }
RED4ext::CName Name(uint64_t value) { RED4ext::CName name;name.hash=value;return name; }
template<class T> uintptr_t Table()
{
    // The prototypes are never exposed as guest objects or destroyed. Tables
    // remain valid even when their binding becomes a tombstone.
    static const uintptr_t table=[] { auto* object=new T;uintptr_t value{};std::memcpy(&value,static_cast<const void*>(object),8);return value; }();
    return table;
}
}
struct RttiAdapter::State : std::enable_shared_from_this<State>
{
    static constexpr size_t Capacity=32;
    static auto& Slots() { static auto* slots=new std::array<std::atomic<std::shared_ptr<State>>,Capacity>;return *slots; }
    static inline std::atomic<size_t> next{};
    Domain::Ptr domain;
    Domain::Ticket owner;
    SharedPointers pointers;
    ViewArena& arena;
    RttiServices services;
    void* native{};
    void* guest{};
    size_t index{};
    uintptr_t typeTable{};
    std::atomic<bool> published{};
    std::mutex control;
    std::vector<void*> views;
    State(Domain::Ptr d,ViewArena& a,SharedPointers p,RttiServices s,void* n)
        : domain(std::move(d)),owner(domain->Retain()),pointers(std::move(p)),arena(a),services(std::move(s)),native(n) {}
    ~State()
    {
        Domain::Scope cleanup(owner);
        services={};pointers.lifetime.reset();
    }
    static std::shared_ptr<State> Load(size_t index)
    {
        auto state=Slots()[index].load();if(!state)throw Failure(Error::Retired);return state;
    }
    struct Call
    {
        std::shared_ptr<State> state;
        Domain::Scope scope;
        uintptr_t native{};
        Call(size_t index,const void* guest,TypeId type)
            : state(Load(index)),scope(*state->domain)
        {
            Check(state->arena.memory().Translate(state->domain->id(),type,Direction::ToNative,
                                                  reinterpret_cast<uintptr_t>(guest),native));
            if(type==profile::RttiSystem && reinterpret_cast<void*>(native)!=state->native)
                throw Failure(Error::IdentityMismatch);
        }
    };
    void* View(RttiObject object, bool requireClass=false)
    {
        if(!object.native)return nullptr;
        if((object.type!=profile::Class && object.type!=profile::RttiMetadata) || (requireClass && object.type!=profile::Class))
            throw Failure(Error::WrongKind);
        std::lock_guard guard(control);
        uintptr_t existing{};
        if(arena.memory().Translate(domain->id(),profile::RttiType,Direction::ToGuest,
                                    reinterpret_cast<uintptr_t>(object.native),existing)==Error::None)
        {
            // Reject a conflicting concrete identity as well as a foreign
            // virtual provider. Never silently upgrade an existing mapping.
            return arena.Borrow(domain,object.type,object.native,1,typeTable);
        }
        auto* view=arena.Borrow(domain,object.type,object.native,1,typeTable);
        try { views.push_back(view); } catch(...) { (void)arena.Retire(view);throw; }
        return view;
    }
    template<size_t Index> struct SystemProxy final : legacy::RttiSystem
    {
        using CName=RED4ext::CName;
        using CClass=RED4ext::CClass;
        using CEnum=RED4ext::CEnum;
        using CBitfield=RED4ext::CBitfield;
        using CBaseFunction=RED4ext::CBaseFunction;
        using CGlobalFunction=RED4ext::CGlobalFunction;
        using CString=RED4ext::CString;
        using IType=RED4ext::rtti::IType;
        template<class T> using DynArray=RED4ext::DynArray<T>;
        Call Enter() { return Call(Index,this,profile::RttiSystem); }
        IType* GetType(CName name) override
        { auto c=Enter();return static_cast<IType*>(c.state->View(c.state->services.getType(name.hash))); }
        IType* GetTypeByAsyncId(uint32_t id) override
        { auto c=Enter();if(!c.state->services.getAsyncType)Unsupported();return static_cast<IType*>(c.state->View(c.state->services.getAsyncType(id))); }
        CClass* GetClass(CName name) override
        { auto c=Enter();return static_cast<CClass*>(c.state->View(c.state->services.getClass(name.hash),true)); }
        CEnum* GetEnum(CName) override { Unsupported(); }
        CBitfield* GetBitfield(CName) override { Unsupported(); }
        void sub_28() override { Unsupported(); }
        CGlobalFunction* GetFunction(CName) override { Unsupported(); }
        void sub_38() override { Unsupported(); }
        void GetNativeTypes(DynArray<IType*>&) override { Unsupported(); }
        void GetGlobalFunctions(DynArray<CBaseFunction*>&) override { Unsupported(); }
        void sub_50() override { Unsupported(); }
        void GetClassFunctions(DynArray<CBaseFunction*>&) override { Unsupported(); }
        void GetEnums(DynArray<CEnum*>&,bool) override { Unsupported(); }
        void GetBitfields(DynArray<CBitfield*>&,bool) override { Unsupported(); }
        void GetClasses(CClass*,DynArray<CClass*>&,bool(*)(CClass*),bool) override { Unsupported(); }
        void GetDerivedClasses(CClass*,DynArray<CClass*>&) override { Unsupported(); }
        void RegisterType(IType* type,uint32_t id) override
        {
            auto c=Enter();if(!c.state->services.registerType)Unsupported();
            if(!type)throw Failure(Error::UnknownPointer);
            uintptr_t native{};
            Check(c.state->arena.memory().Translate(c.state->domain->id(),profile::RttiType,Direction::ToNative,
                                                    reinterpret_cast<uintptr_t>(type),native));
            // The native RTTI registry can retain the object even when its
            // insertion path throws. Keep defining-module/backing ownership.
            c.state->published.store(true);
            c.state->services.registerType(reinterpret_cast<void*>(native),id);
        }
        void sub_88() override { Unsupported(); }
        void sub_90() override { Unsupported(); }
        void UnregisterType(IType*) override { Unsupported(); }
        void RegisterFunction(CGlobalFunction*) override { Unsupported(); }
        void UnregisterFunction(CGlobalFunction*) override { Unsupported(); }
        void sub_B0() override { Unsupported(); }
        void sub_B8() override { Unsupported(); }
        void AddRegisterCallback(legacy::Callback callback) override { Add(callback,false); }
        void AddPostRegisterCallback(legacy::Callback callback) override { Add(callback,true); }
        void Add(legacy::Callback& callback,bool post)
        {
            auto state=Load(Index);
            Domain::Scope cleanup(state->owner);
            // The compiler destroys by-value parameters after local scopes.
            // Consume the payload here so its guest destructor remains inside
            // the retained domain even when conversion or the sink throws.
            struct Consume
            {
                legacy::Callback& value;
                ~Consume() { auto* handler=value.handler;value.handler=nullptr;if(handler)handler->destruct(value.buffer); }
            } consume{callback};
            if(state->domain->IsClosed())throw Failure(Error::Closed);
            auto c=Enter();
            if(!state->services.addCallback)Unsupported();
            auto bridge=std::shared_ptr<CallbackBridge<void(),32>>(CallbackBridge<void(),32>::Copy(
                state->domain,std::as_writable_bytes(std::span(callback.buffer)),callback.handler));
            std::function<void()> invoke=[state,bridge=std::move(bridge)] { (*bridge)(); };
            // Sink invocation is the publication boundary, not its return.
            state->published.store(true);
            state->services.addCallback(std::move(invoke),post);
        }
        void sub_D0() override { Unsupported(); }
        void sub_D8() override { Unsupported(); }
        void CreateScriptedClass(CName,CClass::Flags,CClass*) override { Unsupported(); }
        void CreateScriptedEnum(CName,int8_t,DynArray<uint64_t>&) override { Unsupported(); }
        void CreateScriptedBitfield(CName,DynArray<uint64_t>&) override { Unsupported(); }
        void InitializeScriptRuntime() override { Unsupported(); }
        void RegisterScriptName(CName native,CName script) override
        { auto c=Enter();if(!c.state->services.registerScriptName)Unsupported();c.state->services.registerScriptName(native.hash,script.hash); }
        CClass* GetClassByScriptName(CName name) override
        { auto c=Enter();if(!c.state->services.getScriptClass)Unsupported();return static_cast<CClass*>(c.state->View(c.state->services.getScriptClass(name.hash),true)); }
        CEnum* GetEnumByScriptName(CName) override { Unsupported(); }
        CName ConvertNativeToScriptName(CName name) override { return Convert(name,true); }
        CName ConvertScriptToNativeName(CName name) override { return Convert(name,false); }
        CName Convert(CName name,bool toScript)
        { auto c=Enter();if(!c.state->services.convertName)Unsupported();return Name(c.state->services.convertName(name.hash,toScript)); }
        CString* GetStringConst(uint32_t) override { Unsupported(); }
        void SetStringTable(DynArray<CString>&) override { Unsupported(); }
        ~SystemProxy() override { std::terminate(); } // Borrowed engine singleton cannot be deleted.
    };
    template<size_t Index> struct TypeProxy final : legacy::Type
    {
        using CName=RED4ext::CName;
        using CString=RED4ext::CString;
        using BaseStream=RED4ext::BaseStream;
        TypeMetadata Metadata() const
        {
            Call c(Index,this,profile::RttiType);
            return c.state->services.metadata(reinterpret_cast<void*>(c.native));
        }
        CName GetName() const override { return Name(Metadata().name); }
        uint32_t GetSize() const override { return Metadata().size; }
        uint32_t GetAlignment() const override { return Metadata().alignment; }
        RED4ext::rtti::ERTTIType GetType() const override { return static_cast<RED4ext::rtti::ERTTIType>(Metadata().kind); }
        CString GetTypeName() const override { Unsupported(); }
        CName GetComputedName() const override { return Name(Metadata().computedName); }
        void Construct(void*) const override { Unsupported(); }
        void Destruct(void*) const override { Unsupported(); }
        bool IsEqual(const void*,const void*,uint32_t) override { Unsupported(); }
        void Assign(void*,const void*) const override { Unsupported(); }
        void Move(void*,void*) const override { Unsupported(); }
        bool Unserialize(BaseStream*,void*,int64_t) const override { Unsupported(); }
        bool ToString(const void*,CString&) const override { Unsupported(); }
        bool FromString(void*,const CString&) const override { Unsupported(); }
        bool sub_78() override { Unsupported(); }
        bool sub_80(int64_t,void*) override { Unsupported(); }
        bool sub_88(int64_t,void*) override { Unsupported(); }
        bool sub_90(int64_t,void*,CString&,int64_t) override { Unsupported(); }
        bool sub_98(int64_t,void*,CString&,int64_t,bool) override { Unsupported(); }
        bool sub_A0(int64_t,CString&,bool) override { Unsupported(); }
        bool sub_A8() override { Unsupported(); }
        void sub_B0(int64_t,int64_t) override { Unsupported(); }
        RED4ext::Memory::IAllocator* GetAllocator() const override { Unsupported(); }
        void sub_C0() override { Unsupported(); }
        uint32_t GetMaxAlignment() const override { Unsupported(); }
        bool sub_D0() const override { Unsupported(); }
        void ConstructCls(void*) const override { Unsupported(); }
        void DestructCls(void*) const override { Unsupported(); }
        void* AllocMemory() const override { Unsupported(); }
        ~TypeProxy() override { std::terminate(); }
    };
    template<size_t... I> static auto Tables(std::index_sequence<I...>)
    { return std::array<std::array<uintptr_t,2>,sizeof...(I)>{{{Table<SystemProxy<I>>(),Table<TypeProxy<I>>()}...}}; }
};
RttiAdapter::RttiAdapter(Domain::Ptr domain,ViewArena& arena,SharedPointers pointers,RttiServices services,void* native)
{
    if(!domain || !native || !pointers.lifetime || !pointers.translator.translate ||
       !services.getType || !services.getClass || !services.metadata)throw Failure(Error::InvalidSchema);
    if(domain->source()!=profile::Core.source || domain->target()!=profile::Core.target)throw Failure(Error::IdentityMismatch);
    static const auto tables=State::Tables(std::make_index_sequence<State::Capacity>{});
    auto state=std::make_shared<State>(std::move(domain),arena,std::move(pointers),std::move(services),native);
    auto index=State::next.load();
    do { if(index>=State::Capacity)throw Failure(Error::Capacity); } while(!State::next.compare_exchange_weak(index,index+1));
    state->index=index;state->typeTable=tables[index][1];
    state->guest=arena.Borrow(state->domain,profile::RttiSystem,native,1,tables[index][0]);
    State::Slots()[index].store(state);state_=std::move(state);
}
RttiAdapter::~RttiAdapter() { (void)Retire(); }
void* RttiAdapter::guest() const noexcept { return state_ ? state_->guest : nullptr; }
void* RttiAdapter::View(RttiObject object)
{ if(!state_)throw Failure(Error::Retired);Domain::Scope call(*state_->domain);return state_->View(object); }
Error RttiAdapter::Retire() noexcept
{
    if(!state_)return Error::Retired;
    // Control-plane retirement requires the caller to drain incoming guest
    // calls. Published callbacks have no removal contract and retain the bank.
    if(state_->published.load())return Error::Published;
    try
    {
        std::lock_guard guard(state_->control);
        for(auto* view:state_->views)Check(state_->arena.Retire(view));
        Check(state_->arena.Retire(state_->guest));
        State::Slots()[state_->index].store(nullptr);
    }
    catch(...) { return Error::UnsupportedValue; }
    state_.reset();return Error::None;
}
}
#endif
