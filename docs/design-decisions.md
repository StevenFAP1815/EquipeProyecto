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

  ## DD-007: Strong typedefs con wrapper struct sobre std::string
- Etapa: 1
- Contexto: los IDs del dominio (nodos, calles, restaurantes, repartidores,
  pedidos) son cadenas como "n0", "s3", "r1", "c5", "o17". Necesitamos que
  el compilador impida mezclarlos.
- Alternativas: using alias (cero seguridad), enum class (los IDs no son
  enteros), wrapper struct (seguro, elegido).
- Decision: wrapper struct con constructor explicito, comparadores ==, !=, <,
  especializacion de std::hash y operator<<.
- Justificacion: `using OrderId = std::string` haria que OrderId y CourierId
  fueran el mismo tipo; el compilador aceptaria pasarlos intercambiados.
  El wrapper previene ese bug en tiempo de compilacion.
- Consecuencias: verbosidad reducida por macro DELIVERY_DEFINE_ID_TYPE;
  requiere especializar std::hash por tipo (hecho via TypeName##Hash).

## DD-008: Jerarquia de excepciones para errores de configuracion
- Etapa: 1
- Contexto: el loader puede fallar de muchas formas (archivo ilegible, JSON
  mal formado, campo faltante, tipo incorrecto, nodo huerfano, ID duplicado).
- Alternativas: std::optional + out-param, std::variant<Config, Error>,
  excepciones con jerarquia.
- Decision: excepciones con jerarquia (ConfigError base, cuatro derivadas:
  FileError, JsonError, SchemaError, ReferenceError).
- Justificacion: main captura ConfigError con un solo catch; los tests
  capturan el tipo especifico sin depender del texto del mensaje.
- Consecuencias: el flujo es lineal en main; los mensajes de error incluyen
  contexto (id, campo, valor).

## DD-009: Uso de std::string_view en helpers de validacion JSON
- Etapa: 1
- Contexto: los helpers de ConfigLoader reciben claves y contextos como
  literales. Usar const std::string& provocaba warnings -Wdangling-reference
  en GCC 13+ porque el compilador no puede probar que el const json& devuelto
  no dependa del temporal creado para el parametro.
- Alternativas: std::string (crea copias), const char* (pierde longitud),
  std::string_view (no posee, cero copias).
- Decision: std::string_view en todos los parametros de entrada.
- Justificacion: elimina el warning, evita copias, es el tipo correcto para
  "cadena no propietaria de solo lectura".
- Consecuencias: concatenaciones requieren std::string(key) explicito;
  nlohmann/json v3.11 acepta string_view en find().

## DD-010: Config anidada con indices auxiliares
- Etapa: 1
- Contexto: el JSON tiene secciones anidadas (map, fleet, orders, dispatch,
  incidents, simulation) mas arrays de nodos, calles y restaurantes.
- Alternativas: Config plana, Config anidada, nlohmann::json directo.
- Decision: Config anidada (sub-structs por seccion) + indices auxiliares
  unordered_map<Id, size_t> para busquedas O(1).
- Justificacion: refleja el JSON, agrupa por dominio, y evita lookups lineales
  durante la simulacion.
- Consecuencias: ConfigLoader construye los indices tras validar unicidad;
  cualquier busqueda de nodo por id es O(1).

## DD-011: Catch2 v3 via FetchContent para tests
- Etapa: 1
- Contexto: el proyecto no exige framework de tests, pero sin tests cualquier
  refactor de Etapa 3+ es ruleta rusa.
- Alternativas: asserts caseros, Catch2, GoogleTest, Doctest.
- Decision: Catch2 v3 via FetchContent, con find_package como primera opcion.
- Justificacion: header-only/amalgamado, cero dependencias del sistema,
  integracion con CTest via catch_discover_tests, target separado que no
  contamina el binario de produccion.
- Consecuencias: el target delivery_tests no viola la restriccion del
  proyecto (binario delivery_sim sigue siendo STL + OpenCV + JSON).

## DD-012: GeoBounds con proyeccion equirectangular
- Etapa: 1
- Contexto: convertir lat/lon a pixeles sobre una imagen de mapa.
- Alternativas: Mercator (compleja, overkill), equirectangular (suficiente
  a escala de ciudad).
- Decision: equirectangular simple con latitud invertida en Y.
- Justificacion: la zona cubre ~3 km; la curvatura terrestre es despreciable.
  La formula es legible, testeable y sin dependencias.
- Consecuencias: fuera del bounding box, los pixeles caen fuera de la imagen;
  el llamador decide si clampear. fromPixel permite round-trip para tests.

## DD-013: main solo orquesta, sin logica de validacion
- Etapa: 1
- Contexto: main debe cargar config, manejar errores y salir. La validacion
  vive en ConfigLoader.
- Alternativas: validar en main (intestable), validar en loader (testeable).
- Decision: main solo invoca loadConfigFromFile y captura ConfigError.
- Justificacion: separacion de responsabilidades; tests pueden invocar
  parseConfig sin tocar disco ni main.
- Consecuencias: main cambia poco entre etapas; agregar --log es un cambio
  local.

## DD-014: JSONs invalidos en tests/data, no en config/
- Etapa: 1
- Contexto: los tests requieren fixtures invalidos para verificar rechazos.
- Alternativas: ponerlos en config/ (contaminan el entregable), ponerlos en
  tests/data/ (aislados).
- Decision: tests/data/ con archivos nombrados invalid_*.json.
- Justificacion: config/ esta reservado para configuraciones de simulacion
  reales (equipetrol.json y experimentos de DP.10). tests/data/ contiene
  fixtures de test.
- Consecuencias: los tests son autocontenidos; mover equipetrol.json en
  Etapa 3 no afecta a los tests.