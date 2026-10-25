#include <compat/FrameAdapters.hpp>
#include <compat/Profile.hpp>

namespace compat
{
namespace
{
void Validate(const Domain::Ptr& domain, const SharedPointers& pointers)
{
    if (!domain || !pointers.lifetime || !pointers.translator.translate) throw Failure(Error::InvalidSchema);
    if (domain->source() != profile::Core.source || domain->target() != profile::Core.target)
        throw Failure(Error::IdentityMismatch);
}
void* Translate(const SharedPointers& pointers, TypeId type, Direction direction, void* source)
{
    uintptr_t target{};
    if (source) Check(pointers.translator.translate(pointers.translator.context,type,direction,reinterpret_cast<uintptr_t>(source),target));
    return reinterpret_cast<void*>(target);
}
}
bool ScalarResult::Valid() const noexcept
{
    return (size==0 && alignment==1) || ((size==1 || size==2 || size==4 || size==8) &&
        alignment && alignment<=size && (alignment & (alignment-1))==0);
}
void ScalarResult::CheckOutput(const void* output) const
{
    if (!Valid() || (size ? !output || reinterpret_cast<uintptr_t>(output)%alignment : output!=nullptr))
        throw Failure(Error::InvalidRange);
}
ScriptRoute::Registration BindScript(Domain::Ptr domain, ScriptEntry legacy, SharedPointers pointers, ScalarResult result)
{
    Validate(domain,pointers);
    if (!legacy || !result.Valid()) throw Failure(Error::InvalidCallback);
    return ScriptRoute::Bind(domain,[domain,pointers=std::move(pointers),legacy,result](void* context,void* frame,void* output,int64_t extra) {
        if (!frame || reinterpret_cast<uintptr_t>(frame)%8) throw Failure(Error::InvalidRange);
        result.CheckOutput(output);
        static const ValueMarshaller plan(profile::Core,profile::StackFrame);
        BorrowedValue binding(domain->id(),plan,{static_cast<std::byte*>(frame),plan.type().native_size},pointers.translator);
        auto* guestContext=Translate(pointers,profile::Instance,Direction::ToGuest,context);
        binding.CallGuest([&](void* guest) { legacy(guestContext,guest,output,extra); });
    });
}
OpcodeTable::OpcodeTable(Domain::Ptr domain, SharedPointers pointers, std::span<const Operation> operations)
{
    Validate(domain,pointers);
    std::array<bool,256> used{};
    if (operations.empty() || operations.size()>256) throw Failure(Error::InvalidSchema);
    for (const auto& op:operations)
    {
        if (!op.native || !op.result.Valid() || used[op.opcode]) throw Failure(Error::InvalidSchema);
        used[op.opcode]=true;
    }
    // Do not free/recycle the data table while stale callers can still index it.
    // Retiring its compiled entries releases owners; the table itself has no owner.
    static auto* tables=new std::array<std::array<OpcodeEntry,256>,32>{};
    static std::atomic<size_t> next{};
    auto index=next.load(std::memory_order_relaxed);
    do { if(index==tables->size()) throw Failure(Error::Capacity); }
    while(!next.compare_exchange_weak(index,index+1,std::memory_order_relaxed));
    entries_=&(*tables)[index]; routes_.reserve(operations.size()+1);
    routes_.push_back(Route::Bind(domain,[](void*,void*,void*,void*) { throw Failure(Error::UnknownOperation); }));
    entries_->fill(routes_.back().entry());
    for (const auto& op:operations)
    {
        routes_.push_back(Route::Bind(domain,[domain,pointers,op](void* context,void* frame,void* output,void* auxiliary) {
            if(auxiliary) throw Failure(Error::UnsupportedValue);
            op.result.CheckOutput(output);
            auto* binding=BorrowedValue::Find(domain->id(),profile::StackFrame,frame);
            if(!binding) throw Failure(Error::UnknownPointer);
            auto* nativeContext=Translate(pointers,profile::Instance,Direction::ToNative,context);
            binding->CallNative([&](void* native) { op.native(nativeContext,native,output,nullptr); });
        }));
        (*entries_)[op.opcode]=routes_.back().entry();
    }
}
OpcodeTable::~OpcodeTable() { if(!published_) (void)Retire(); }
void OpcodeTable::Publish() noexcept
{ for(auto& route:routes_)route.Publish();published_=true; }
Error OpcodeTable::Retire() noexcept
{
    if(published_)return Error::Published;
    for(auto& route:routes_) (void)route.Retire();
    routes_.clear();return Error::None;
}
}
