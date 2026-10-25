#pragma once
#ifdef _WIN32
#include <compat/RttiAdapter.hpp>
namespace RED4ext { struct CRTTISystem; }
namespace compat
{
// Current-SDK engine bindings. The integration owns the verified native system
// and its lifetime; legacy pointers may enter only through RttiAdapter's maps.
// No historical object is reinterpreted as a native engine type here.
RttiServices BindNativeRtti(RED4ext::CRTTISystem* system);
}
#endif
