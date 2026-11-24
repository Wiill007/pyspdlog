cmake_minimum_required(VERSION 3.15...3.31)
# La versión del paquete vive en pyproject.toml
project(pyspdlog LANGUAGES CXX)

# Release por defecto: los bindings en Debug son mucho más lentos
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release MinSizeRel RelWithDebInfo)
endif()

# Símbolos ocultos por defecto, así el spdlog estático que metemos en el
# módulo no choca con otras extensiones que también traigan spdlog
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# --- Python + nanobind -------------------------------------------------------
# SABIModule es opcional: permite la ABI estable (abi3) en Python >= 3.12
find_package(Python 3.10
  REQUIRED COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

# Le pedimos a nanobind (instalado con pip/uv) dónde está su config de CMake.
# Con scikit-build-core también funciona porque está en el entorno de build.
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE nanobind_ROOT)
find_package(nanobind CONFIG REQUIRED)

# --- spdlog ------------------------------------------------------------------
option(PYSPDLOG_USE_SYSTEM_SPDLOG
  "Usar el spdlog del sistema en vez de descargarlo (el módulo dependerá de libspdlog.so)"
  OFF)

if(PYSPDLOG_USE_SYSTEM_SPDLOG)
  find_package(spdlog 1.12 CONFIG REQUIRED)
else()
  include(FetchContent)
  # spdlog va estático dentro del módulo, así que necesita -fPIC
  set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
  set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
  # Tarball del release en vez de git: no hace falta git para compilar desde
  # el sdist, y el hash verifica lo que se descarga
  FetchContent_Declare(spdlog
    URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744)
  FetchContent_MakeAvailable(spdlog)
endif()

# --- Módulo de Python --------------------------------------------------------
nanobind_add_module(_core
  STABLE_ABI  # una sola wheel abi3 para Python >= 3.12 (se ignora en versiones anteriores)
  # Registro de tipos propio: no choca con otras extensiones nanobind que
  # también expongan tipos de spdlog, y set_leak_warnings() solo nos afecta a nosotros
  NB_DOMAIN pyspdlog
  src/cpp/module.cpp
  src/cpp/logger.cpp
  src/cpp/registry.cpp
  src/cpp/sinks.cpp
  src/cpp/threads.cpp)

target_link_libraries(_core PRIVATE spdlog::spdlog)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(_core PRIVATE -Wall -Wextra)
endif()

# Queda como pyspdlog/_core.*.so (scikit-build-core lo mete en la wheel)
install(TARGETS _core LIBRARY DESTINATION pyspdlog)

# --- Stubs de tipos (_core.pyi) ---------------------------------------------
# Se generan importando el módulo recién compilado, así que no funciona al
# compilar para otra arquitectura: ahí hay que desactivarlo.
option(PYSPDLOG_STUBS "Generar _core.pyi con nanobind stubgen" ON)

if(PYSPDLOG_STUBS)
  nanobind_add_stub(_core_stub
    MODULE _core
    OUTPUT _core.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:_core>
    DEPENDS _core)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_core.pyi DESTINATION pyspdlog)
endif()
