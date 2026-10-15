# Registro de decisiones de diseño (ADR)

Formato: contexto, alternativas, decisión, justificación, consecuencias.

## DD-001: Estructura por capas de dominio
- Etapa: 0
- Contexto: organizar el código separando dominio de concurrencia.
- Alternativas: plana, por capas técnicas, por capas de dominio.
- Decisión: por capas de dominio (`core/`, `infra/`, `concurrency/`, `sim/`, `viz/`, `app/`).
- Justificación: aísla la concurrencia en un solo lugar, facilita el mapa de conceptos.
- Consecuencias: disciplina al incluir headers; `core/` nunca incluye `<thread>`.

## DD-002: CMake ≥ 3.16 y C++17 estricto
- Etapa: 0
- Contexto: requisitos del proyecto (CMake ≥ 3.11, C++17).
- Decisión: `cmake_minimum_required(VERSION 3.16)`, `CMAKE_CXX_EXTENSIONS OFF`.
- Justificación: FetchContent moderno, portabilidad estricta.
- Consecuencias: requiere toolchain actualizado.

## DD-003: nlohmann/json vía FetchContent
- Etapa: 0
- Contexto: el proyecto permite cualquier biblioteca JSON.
- Decisión: FetchContent con tag `v3.11.3`.
- Justificación: reproducibilidad sin pasos manuales.
- Consecuencias: requiere red al configurar; plan B vía `pacman`.

## DD-004: Señales con std::signal + atomic + CV
- Etapa: 0 (diseño), 9 (implementación)
- Contexto: el proyecto exige SIGINT/SIGTERM con apagado limpio en 5s.
- Alternativas: self-pipe trick, `sigwait`, `std::signal` + atomic.
- Decisión: `std::signal` + `std::atomic<bool>` + `condition_variable`.
- Justificación: portable Windows/Linux, suficiente en práctica; el handler solo escribe un atómico.
- Consecuencias: en Windows SIGTERM no se envía nativamente; documentado.

## DD-005: Hilo render dedicado con OpenCV
- Etapa: 0 (diseño), 4 (implementación)
- Contexto: `cv::imshow` debe bombearse en el hilo que crea la ventana.
- Alternativas: renderizar en `main`, hilo render dedicado.
- Decisión: hilo render dedicado; `main` solo orquesta y hace `join`.
- Justificación: `waitKey` en `main` complica la detección de señales.
- Consecuencias: el render usa sleep para FPS fijo (permitido por sección 4).

## DD-006: ThreadSanitizer se ejecuta en WSL2, no en Windows
- Etapa: 0
- Contexto: la rúbrica exige TSan sin reportes; MinGW UCRT64 no provee libtsan.
- Alternativas: intentar compilar libtsan desde fuente, usar Clang en Windows,
  usar WSL2 con Ubuntu.
- Decisión: DP.11 (análisis con herramientas) se ejecuta en WSL2 con Ubuntu.
  El código se mantiene portable y compila igual en ambos entornos.
- Justificación: MSYS2/MinGW no incluye libtsan precompilada; compilarla desde
  fuente es inviable en el plazo del proyecto. WSL2 provee g++/clang con TSan,
  Valgrind y Helgrind, cubriendo todos los requisitos de DP.11.
- Consecuencias: el CMake `ENABLE_TSAN=ON` seguirá existiendo y funcionará en
  Linux/WSL2; en Windows se documenta como no soportado por el toolchain.