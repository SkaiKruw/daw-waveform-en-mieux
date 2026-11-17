cmake_minimum_required(VERSION 3.22)
project(OndeProto VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ONDE_BUILD_APP "Construire l'application JUCE / Tracktion Engine" ON)
# Dépendances : dossier deps/ du dépôt (créé par windows/installer-et-lancer.ps1), sinon ../deps (ancienne disposition).
if(EXISTS "${CMAKE_SOURCE_DIR}/deps/tracktion_engine/modules/juce/CMakeLists.txt" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/../deps/tracktion_engine")
    set(ONDE_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")
else()
    set(ONDE_DEPS_DIR "${CMAKE_SOURCE_DIR}/../deps")
endif()
set(TRACKTION_ENGINE_DIR "${ONDE_DEPS_DIR}/tracktion_engine" CACHE PATH "Chemin vers tracktion_engine (avec le sous-module JUCE)")

if(MSVC)
    # Les sources sont en UTF-8 (accents de l'interface) : MSVC doit le savoir.
    add_compile_options(/utf-8)
endif()

add_subdirectory(core)
if(ONDE_BUILD_APP)
    add_subdirectory(app)
endif()
