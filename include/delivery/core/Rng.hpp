#pragma once

#include <cstdint>

namespace delivery::core {

// ---------------------------------------------------------------------------
// Utilidades de aleatoriedad determinista.
//
// El proyecto exige reproducibilidad: con la misma seed, la misma secuencia
// de pedidos. std::mt19937_64 es un generador estandar, deterministico y
// rapido. NO es thread-safe: cada hilo que necesite numeros aleatorios debe
// tener su propia instancia.
//
// Fila 2 del mapa de conceptos: cuando varios hilos necesiten aleatoriedad
// (Etapa 3+), se derivara una sub-seed por hilo con deriveSeed(). Esto
// garantiza que cada hilo tenga una secuencia independiente pero
// reproducible desde la seed maestra.
// ---------------------------------------------------------------------------

// Deriva una sub-seed determinista a partir de una seed maestra y un
// identificador de flujo (por ejemplo, el indice del hilo).
//
// Usa el algoritmo SplitMix64: deterministico, bien distribuido, sin estado
// compartido. Dos llamadas con los mismos argumentos devuelven el mismo
// resultado.
inline std::uint64_t deriveSeed(std::uint64_t masterSeed,
                                std::uint64_t streamId) noexcept {
    std::uint64_t z = masterSeed + 0x9e3779b97f4a7c15ULL * (streamId + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

}  // namespace delivery::core