#include <functional>
#include <memory>
#include <utility>
#include <cstdio>
#include <RED4ext/Callback.hpp>

using OldCallback = RED4ext::Callback<void (*)(), 32>;
using NewCallback = RED4ext::Callback<void (*)(), 64>;
static_assert(sizeof(OldCallback) == 0x28);
static_assert(sizeof(NewCallback) == 0x48);
static_assert(offsetof(OldCallback, handler) == 0x20);
static_assert(offsetof(NewCallback, handler) == 0x40);

struct Counts { int live = 0; int copies = 0; int moves = 0; int calls = 0; };
struct Payload
{
    Counts* counts;
    explicit Payload(Counts& c) : counts(&c) { ++counts->live; }
    Payload(const Payload& p) : counts(p.counts) { ++counts->live; ++counts->copies; }
    Payload(Payload&& p) noexcept : counts(p.counts) { ++counts->live; ++counts->moves; }
    ~Payload() { --counts->live; }
    void fire() const { ++counts->calls; }
};

template<typename Callback>
void copy_into(Callback& destination, Callback& source)
{
    if (destination.handler) destination.handler->destruct(destination.buffer);
    destination.handler = source.handler;
    if (source.handler) source.handler->copy(destination.buffer, source.buffer);
}

NewCallback adapt(OldCallback& legacy)
{
    auto owner = std::make_shared<OldCallback>();
    copy_into(*owner, legacy);
    return NewCallback([owner]() { (*owner)(); });
}

NewCallback create(Counts& counts)
{
    OldCallback legacy([payload = Payload(counts)]() { payload.fire(); });
    return adapt(legacy);
}

int main()
{
    Counts counts;
    {
        auto bridged = create(counts);
        if (counts.live != 1) return 1;
        NewCallback copied;
        copy_into(copied, bridged);
        auto moved = std::move(copied);
        NewCallback assigned;
        copy_into(assigned, bridged);
        NewCallback moveAssigned;
        moveAssigned = std::move(assigned);
        bridged(); moved(); moveAssigned();
        if (counts.calls != 3 || counts.live != 1) return 2;
    }
    if (counts.live != 0 || counts.copies < 1) return 3;
    std::printf("PASS callback bridge: old=0x%zx new=0x%zx calls=%d copies=%d live=%d\n",
                sizeof(OldCallback), sizeof(NewCallback), counts.calls, counts.copies, counts.live);
    return 0;
}
