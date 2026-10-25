#include <compat/EarlyModules.hpp>
#include <algorithm>
#include <limits>
namespace compat
{
static_assert(std::atomic<unsigned>::is_always_lock_free && std::atomic<size_t>::is_always_lock_free &&
              std::atomic<Error>::is_always_lock_free);
namespace
{
wchar_t Fold(wchar_t c) noexcept
{ return c==L'/' ? L'\\' : c>=L'A' && c<=L'Z' ? c+L'a'-L'A' : c; }
}
EarlyModules::EarlyModules(size_t capacity):capacity_(capacity)
{
    if(!capacity || capacity>4096)throw Failure(Error::Capacity);
    records_=std::make_unique<Record[]>(capacity);
}
bool EarlyModules::Prefix(std::wstring_view value,std::wstring_view prefix) noexcept
{
    if(value.size()<prefix.size())return false;
    for(size_t i=0;i<prefix.size();++i)if(Fold(value[i])!=Fold(prefix[i]))return false;
    return true;
}
void EarlyModules::AddRoot(std::wstring directory,uint32_t domain)
{
    if(sealed())throw Failure(Error::Closed);
    if(!domain || directory.size()<3 || directory.size()>1024 || directory[1]!=L':' ||
       Fold(directory[2])!=L'\\' || Fold(directory[0])<L'a' || Fold(directory[0])>L'z')throw Failure(Error::InvalidRange);
    for(auto& c:directory)
    {
        // ASCII roots permit deterministic ordinal folding without locale or
        // Windows calls in notification context. Filenames may contain Unicode.
        if(c<32 || c>126)throw Failure(Error::UnsupportedValue);
        c=Fold(c);
    }
    if(directory.back()!=L'\\')directory.push_back(L'\\');
    if(directory.find(L"\\..\\")!=std::wstring::npos || directory.find(L"\\.\\")!=std::wstring::npos ||
       directory.find(L"\\\\")!=std::wstring::npos)throw Failure(Error::InvalidRange);
    for(const auto& root:roots_)
        if(Prefix(directory,root.directory) || Prefix(root.directory,directory))throw Failure(Error::Overlap);
    if(roots_.size()==16)throw Failure(Error::Capacity);
    roots_.push_back({std::move(directory),domain});
}
void EarlyModules::Seal()
{
    if(roots_.empty())throw Failure(Error::InvalidSchema);
    if(sealed_.exchange(true,std::memory_order_release))throw Failure(Error::Closed);
}
Error EarlyModules::Fail(Error error) noexcept
{ Error empty=Error::None;failure_.compare_exchange_strong(empty,error);return error; }
Error EarlyModules::Loaded(uintptr_t base,size_t size,std::wstring_view path) noexcept
{
    if(!sealed())return Fail(Error::Closed);
    if(!base || !size || size>UINTPTR_MAX-base || path.empty())return Fail(Error::InvalidRange);
    // The OS serializes notification writers through its loader lock. Readers
    // are concurrent; a record is immutable once its ready state is published.
    auto index=count_.load(std::memory_order_relaxed);
    for(size_t i=0;i<index;++i)
    {
        const auto& old=records_[i];
        if(old.state.load(std::memory_order_acquire)==1 && base<old.base+old.size && old.base<base+size)
            return Fail(Error::Overlap);
    }
    if(path.size()>=4 && ((path[0]==L'\\' && path[1]==L'?' && path[2]==L'?' && path[3]==L'\\') ||
                         (path[0]==L'\\' && path[1]==L'\\' && path[2]==L'?' && path[3]==L'\\')))
        path.remove_prefix(4);
    do {if(index==capacity_)return Fail(Error::Capacity);}
    while(!count_.compare_exchange_weak(index,index+1,std::memory_order_acq_rel));
    auto& record=records_[index];record.base=base;record.size=size;
    for(const auto& root:roots_)if(Prefix(path,root.directory)){record.domain=root.domain;break;}
    record.state.store(1,std::memory_order_release);
    return Error::None;
}
Error EarlyModules::Unloaded(uintptr_t base) noexcept
{
    if(!sealed())return Fail(Error::Closed);
    for(auto i=count_.load(std::memory_order_acquire);i>0;--i)
    {
        auto& record=records_[i-1];
        if(record.state.load(std::memory_order_acquire)==1 && record.base==base)
        {record.state.store(2,std::memory_order_release);return Error::None;}
    }
    // Existing native modules may predate notification registration.
    return Error::None;
}
EarlyModules::Match EarlyModules::Find(uintptr_t instruction) const noexcept
{
    if(!sealed())return {Error::Closed};
    if(const auto error=failure_.load(std::memory_order_acquire);error!=Error::None)return {error};
    for(auto i=count_.load(std::memory_order_acquire);i>0;--i)
    {
        const auto& record=records_[i-1];const auto state=record.state.load(std::memory_order_acquire);
        if(state && instruction>=record.base && instruction-record.base<record.size)
            return {state==1 ? Error::None : Error::Retired,record.domain,record.base,record.size};
    }
    return {};
}
}
