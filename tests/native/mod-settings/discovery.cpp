#include "discovery.hpp"

int discoveredRegistrations = 0, discoveredDescriptions = 0, discoveredExpansions = 0;

template<> void Red::ClassDefinition<DiscoveryProbe>::RegisterType() {
    ++discoveredRegistrations;
}
template<> void Red::ClassDefinition<DiscoveryProbe>::DescribeType() {
    Red::TypeInfoBuilder<Red::Scope::For<DiscoveryProbe>()>::Describe(nullptr);
}
template<> void Red::ClassExpansion<DiscoveryProbe, Red::Scope::For<DiscoveryExpansion>()>::DescribeType() {
    Red::TypeInfoBuilder<Red::Scope::For<DiscoveryExpansion>()>::Describe(nullptr);
}
