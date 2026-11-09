# Análisis con herramientas (DP.11)

> Las salidas completas y **sin editar** de cada ejecución están en [`analysis/raw/`](../analysis/raw/).

## Entorno

| | Versión |
|---|---|
| Sistema | Ubuntu 26.04.1 LTS en WSL2 (kernel 6.18.54.1) |
| Compilador | g++ 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| CMake | 4.2.3 |
| OpenCV | 4.10.0 (backend de ventanas Qt5) |
| ThreadSanitizer | el incluido en GCC 15.2 (`libtsan.so.2`) |
| AddressSanitizer + LeakSanitizer | los incluidos en GCC 15.2 (`libasan.so.8`) |

Archivo de versiones: [`analysis/raw/versions.txt`](../analysis/raw/versions.txt).

Las herramientas se usan en Ubuntu porque el compilador de Windows (MSYS2) no incluye sanitizers (DD-006). Valgrind no se usa: AddressSanitizer cubre los errores de memoria y las fugas sin instalar nada, y es mucho más rápido, lo que permite observar la ejecución completa en tiempo real.

## Cómo se ejecutó (igual para cada herramienta)

**Configuración:** `config/equipetrol.json` (15 repartidores, bolsa de 3, ráfagas de hasta 12 pedidos, `breakdownProbability` 0,1, `timeScale` 60).

**Ejecución:** con [`tools/run_with_signal.sh`](../tools/run_with_signal.sh), que arranca el programa con la ventana abierta (WSLg), espera la línea `Simulation has started...` y envía **SIGINT a los 45 s**, es decir, 2700 s simulados:

```bash
cd config
bash ../tools/run_with_signal.sh <build>/delivery_sim equipetrol.json 45 <carpeta_de_salida>
```

**Cobertura de cada ejecución**, comprobada en su registro de eventos:

| Ejecución | Ráfagas (instantes con >1 pedido) | Máximo de pedidos en un instante | Averías | Reasignaciones | Apagado por señal | `check_event_log.py` |
|---|---|---|---|---|---|---|
| TSan, antes | 4 | 12 | 4 | 4 | SIGINT, salida en 1,1 s | OK |
| TSan, después | 4 | 12 | 4 | 4 | SIGINT, código 0, 1,1 s | OK |
| ASan + LSan | 4 | 12 | 5 | 10 | SIGINT, código 0, 0,3 s | OK |

---

## Herramienta 1: ThreadSanitizer (condiciones de carrera)

**Compilación:**
```bash
mkdir build-tsan && cd build-tsan
cmake -DENABLE_TSAN=ON .. && make
```

### Primera ejecución: lo que reportó
Salida completa: [`analysis/raw/tsan-antes/stderr.txt`](../analysis/raw/tsan-antes/stderr.txt).

**5 avisos** (en otra ejecución de prueba fueron 6; el número varía con los tiempos), **todos dentro de bibliotecas del sistema**:

| Tipo | Dónde |
|---|---|
| data race | `libQt5Core.so.5` (varios, incluido `QCoreApplicationPrivate::sendPostedEvents` y `QWaitCondition::~QWaitCondition`) |
| data race | `libdbus-1.so.3` (`dbus_connection_get_is_connected`) |
| lock-order-inversion | `libdbus-1.so.3` |

**Análisis de cada aviso** (revisando las dos pilas de cada uno):
- La memoria involucrada es de Qt (`QArrayData`, objetos internos de Qt o dbus), nunca un dato del proyecto.
- Uno de los dos hilos lo crea **Qt por su cuenta** (por ejemplo `QDBusConnection`). El otro es nuestro hilo de la ventana, pero *dentro* de una función de OpenCV/Qt (`cv::imshow`, `cv::waitKey`, `cv::destroyAllWindows`).
- **Por qué TSan avisa:** solo ve la sincronización del código compilado con `-fsanitize=thread`. Qt y dbus vienen precompilados del sistema, así que TSan no ve sus candados internos y cree que hay conflictos. Son **falsos positivos** conocidos de usar TSan con bibliotecas no instrumentadas.
- Sin la ventana (los 70 tests), TSan da **0 avisos**.

### Qué se cambió
Se agregó [`src/analysis/tsan_default_suppressions.cpp`](../src/analysis/tsan_default_suppressions.cpp), que solo se compila con `ENABLE_TSAN`:
```cpp
extern "C" const char* __tsan_default_suppressions() {
    return "race:libQt5Core.so\n"
           "race:libdbus-1.so\n"
           "deadlock:libdbus-1.so\n";
}
```
`__tsan_default_suppressions` es la función que TSan consulta al arrancar. Así **cualquiera** que compile con `-DENABLE_TSAN=ON` obtiene las mismas supresiones, sin variables de entorno. Una supresión `race:<biblioteca>` ignora un aviso solo si alguna función **de esa biblioteca** aparece en la pila del acceso.

**Por qué las supresiones no esconden errores del proyecto:** un acceso a nuestros datos ocurre en nuestro código, sin funciones de Qt en su pila, y seguiría reportándose. Lo comprobamos con un programa de prueba, [`tools/tsan_canary.cpp`](../tools/tsan_canary.cpp): dos hilos incrementan el mismo `int` sin candado. Compilado **con el mismo archivo de supresiones**, TSan **sí** reporta la carrera (`tsan_canary.cpp:13`, código 66). Ver [`analysis/raw/canary/`](../analysis/raw/canary/).

### Segunda ejecución: resultado
Salida completa: [`analysis/raw/tsan-despues/`](../analysis/raw/tsan-despues/). **0 avisos**, código de salida 0.

Tests con TSan ([`analysis/raw/tsan-tests.txt`](../analysis/raw/tsan-tests.txt)): 70/70, 0 avisos. Durante el desarrollo, TSan se ejecutó en cada PR y detectó dos errores **en los tests** (no en el programa), ya corregidos:
- `REQUIRE` de Catch2 usado dentro de varios hilos (PR 14);
- un test que leía dos datos de un repartidor con dos llamadas separadas (PR 18).

---

## Herramienta 2: AddressSanitizer + LeakSanitizer (memoria)

**Compilación:**
```bash
mkdir build-asan && cd build-asan
cmake -DENABLE_ASAN=ON .. && make
```
AddressSanitizer detecta accesos fuera de límites, uso de memoria liberada, doble liberación y otros errores de memoria. LeakSanitizer, activado por defecto en Linux, informa al terminar de toda memoria reservada y nunca liberada.

### Ejecución: lo que reportó
Salida completa: [`analysis/raw/asan/`](../analysis/raw/asan/). **0 errores de memoria y 0 fugas**; `stderr.txt` está vacío y el código de salida es 0.
Tests con ASan ([`analysis/raw/asan-tests.txt`](../analysis/raw/asan-tests.txt)): 70/70, 0 errores.

### Qué se cambió
Nada en el código: no hubo reportes. Se agregó la opción `ENABLE_ASAN` al `CMakeLists.txt`, que no puede combinarse con `ENABLE_TSAN`.

### Observación sobre la compilación con ASan
Al compilar con `-DENABLE_ASAN=ON` aparecen 3 warnings `-Wmaybe-uninitialized` en `std_function.h`, originados en **Catch2** (`catch_matchers_string.cpp`, que usa `std::regex`). Es código de terceros que solo usan los tests, y el warning solo aparece con `-O1` y ASan. Al recompilar **solo el código del proyecto** con ASan hay **0 warnings**, y la compilación normal del brief (`cmake .. && make`) tampoco tiene ninguno.

---

## Reportes que siguen existiendo en la versión final
| Herramienta | Reporte | Explicación |
|---|---|---|
| ThreadSanitizer | Ninguno visible. Están suprimidos 5–6 avisos internos de `libQt5Core`/`libdbus` | Falsos positivos de bibliotecas del sistema no instrumentadas; análisis arriba. |
| AddressSanitizer / LeakSanitizer | Ninguno | — |
