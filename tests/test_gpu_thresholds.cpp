#include <delivery/sim/Dispatcher.hpp>

#include <catch2/catch_test_macros.hpp>

using delivery::sim::Dispatcher;

TEST_CASE("Umbral GPU: el filtro de una flota de la ciudad se queda en la CPU") {
    REQUIRE_FALSE(Dispatcher::gpuFilterWorthIt(3000));
    REQUIRE_FALSE(Dispatcher::gpuFilterWorthIt(Dispatcher::kGpuFilterMinCouriers - 1));
    REQUIRE(Dispatcher::gpuFilterWorthIt(Dispatcher::kGpuFilterMinCouriers));
    REQUIRE(Dispatcher::gpuFilterWorthIt(1000000));
}

TEST_CASE("Umbral GPU: la ETA de un solo pedido se queda en la CPU y un lote grande va a la GPU") {
    REQUIRE_FALSE(Dispatcher::gpuEtaWorthIt(1, 3000));
    REQUIRE_FALSE(Dispatcher::gpuEtaWorthIt(10, 50));
    REQUIRE(Dispatcher::gpuEtaWorthIt(50, 200));
    REQUIRE(Dispatcher::gpuEtaWorthIt(1000, 5000));
}
