#include <compat/ResolverRouter.hpp>

namespace compat
{
ResolverRouter::ResolverRouter(const EarlyModules& modules,NativeResolver native,void* context)
    : modules_(modules),native_(native),context_(context)
{
    if(!native_)throw Failure(Error::InvalidSchema);
}
void ResolverRouter::Add(std::shared_ptr<const AddressCatalog> catalog,Prepare prepare)
{
    if(sealed_)throw Failure(Error::Closed);
    if(!catalog || !catalog->sealed())throw Failure(Error::InvalidSchema);
    for(const auto& existing:catalogs_)
        if(existing.catalog->domain()->id()==catalog->domain()->id())throw Failure(Error::Overlap);
    if(catalogs_.size()==16)throw Failure(Error::Capacity);
    catalogs_.push_back({std::move(catalog),std::move(prepare)});
}
void ResolverRouter::Seal()
{
    if(sealed_)throw Failure(Error::Closed);
    if(!modules_.sealed() || catalogs_.empty())throw Failure(Error::InvalidSchema);
    sealed_=true;
}
ResolverRouter::Result ResolverRouter::Reject(uint32_t primary,uintptr_t caller,uint32_t domain,Error error) const noexcept
{
    auto index=next_.load(std::memory_order_relaxed);
    do
    {
        if(index==DiagnosticCapacity)
        { overflow_.store(true,std::memory_order_release);return {0,error,domain}; }
    } while(!next_.compare_exchange_weak(index,index+1,std::memory_order_relaxed));
    auto& cell=diagnostics_[index];cell.value={caller,primary,domain,error};
    cell.ready.store(true,std::memory_order_release);
    return {0,error,domain};
}
ResolverRouter::Result ResolverRouter::Resolve(uint32_t primary,uintptr_t caller) const
{
    if(!sealed_)return Reject(primary,caller,0,Error::Closed);
    if(!caller)return Reject(primary,caller,0,Error::InvalidRange);
    const auto match=modules_.Find(caller);
    if(match.error!=Error::None)return Reject(primary,caller,match.domain,match.error);
    // Host/native callers retain the original resolver behavior, including its
    // own failure/exception policy. A surrounding legacy TLS scope is irrelevant.
    if(!match.domain)return {native_(context_,primary),Error::None,0};
    for(const auto& binding:catalogs_)if(binding.catalog->domain()->id()==match.domain)
    {
        const auto& catalog=binding.catalog;
        try
        {
            if(catalog->domain()->IsClosed())throw Failure(Error::Closed);
            Domain::Scope scope(*catalog->domain());
            const auto address=catalog->Resolve(primary);
            if(binding.prepare)binding.prepare(match,caller);
            return {address,Error::None,match.domain};
        }
        catch(const Failure& failure) { return Reject(primary,caller,match.domain,failure.code()); }
    }
    return Reject(primary,caller,match.domain,Error::IdentityMismatch);
}
bool ResolverRouter::ReadDiagnostic(size_t index,Diagnostic& output) const noexcept
{
    if(index>=DiagnosticCapacity || !diagnostics_[index].ready.load(std::memory_order_acquire))return false;
    output=diagnostics_[index].value;return true;
}
}
