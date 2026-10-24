#include "common/D3DUpload.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "Check failed: " << #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; std::exit(1); } } while (0)

struct Fence final : ID3D12Fence
{
    enum Mode { Complete, Notify, Never, Failure, Removed } Behavior = Complete;
    uint64_t Completed = 1;
    HANDLE Registered = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**) override { return E_NOTIMPL; }
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override { return Behavior == Removed ? UINT64_MAX : Completed; }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 value,HANDLE event) override
    {
        Registered = event;
        if (Behavior == Failure) return E_FAIL;
        if (Behavior == Notify) { Completed = value; SetEvent(event); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 value) override { Completed = value; if (Registered) SetEvent(Registered); return S_OK; }
};
int main()
{
    Fence fence;
    assert(Graphics::Wait(&fence,1,0) && !fence.Registered);
    fence.Behavior = Fence::Notify; fence.Completed = 0;
    assert(Graphics::Wait(&fence,1,100));
    DWORD flags = 0;
    assert(!GetHandleInformation(fence.Registered,&flags));
    fence.Behavior = Fence::Failure; fence.Completed = 0;
    assert(!Graphics::Wait(&fence,1,0));
    assert(!GetHandleInformation(fence.Registered,&flags));
    fence.Behavior = Fence::Never;
    assert(!Graphics::Wait(&fence,1,0));
    // A timeout must not close an event that is still registered with the GPU.
    assert(GetHandleInformation(fence.Registered,&flags));
    fence.Signal(1); assert(WaitForSingleObject(fence.Registered,0) == WAIT_OBJECT_0);
    CloseHandle(fence.Registered); fence.Registered = nullptr;
    fence.Behavior = Fence::Removed;
    assert(!Graphics::Wait(&fence,1,0));
    std::cout << "CET bounded fence waits, failures and event lifetime checks passed\n";
}
