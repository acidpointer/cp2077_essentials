#pragma once

#include <RED4ext/RED4ext.hpp>
#include <RedLib.hpp>

struct DiscoveryProbe {};
struct DiscoveryExpansion {};
extern int discoveredRegistrations, discoveredDescriptions, discoveredExpansions;

// Replace only the engine-facing operations. Keep the real ClassDefinition,
// ClassExpansion, inline registrars and declaration macros. Merely declaring a
// type must discover it, including when this header appears in multiple TUs.
template<> void Red::ClassDefinition<DiscoveryProbe>::RegisterType();
template<> void Red::ClassDefinition<DiscoveryProbe>::DescribeType();
template<> void Red::ClassExpansion<DiscoveryProbe, Red::Scope::For<DiscoveryExpansion>()>::DescribeType();

RTTI_DEFINE_CLASS(DiscoveryProbe, "DiscoveryProbe", {
    ++discoveredDescriptions;
});
RTTI_EXPAND_CLASS(DiscoveryProbe, DiscoveryExpansion, {
    ++discoveredExpansions;
});
