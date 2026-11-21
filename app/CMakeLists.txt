# Application Onde : JUCE + Tracktion Engine (GPLv3 / licence commerciale, voir README).
add_subdirectory(${TRACKTION_ENGINE_DIR}/modules/juce ${CMAKE_BINARY_DIR}/juce)
add_subdirectory(${TRACKTION_ENGINE_DIR}/modules ${CMAKE_BINARY_DIR}/tracktion)

set(ONDE_JUCE_DEFS
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_LADSPA=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_DISPLAY_SPLASH_SCREEN=0 TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

# --- Worker isolé : charge un plugin VST3 (ou de démo) dans son propre processus -------
juce_add_gui_app(onde-plugin-host PRODUCT_NAME "onde-plugin-host" COMPANY_NAME "Onde" NEEDS_CURL FALSE NEEDS_WEB_BROWSER FALSE)
juce_generate_juce_header(onde-plugin-host)
target_sources(onde-plugin-host PRIVATE Worker/PluginHostMain.cpp)
target_compile_definitions(onde-plugin-host PRIVATE ${ONDE_JUCE_DEFS})
target_compile_features(onde-plugin-host PRIVATE cxx_std_20)
target_link_libraries(onde-plugin-host PRIVATE onde_sandbox juce::juce_audio_processors juce::juce_audio_utils juce::juce_recommended_config_flags)
if(MSVC)
    target_link_options(onde-plugin-host PRIVATE /DEBUG /OPT:REF /OPT:ICF)   # noms de fonctions pour les diagnostics
endif()

# --- Application ------------------------------------------------------------------------
juce_add_gui_app(Onde
    PRODUCT_NAME "Onde" COMPANY_NAME "Onde" BUNDLE_ID "fr.onde.prototype"
    MICROPHONE_PERMISSION_ENABLED TRUE MICROPHONE_PERMISSION_TEXT "Enregistrer une piste audio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-1024.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon-64.png"
    NEEDS_CURL FALSE NEEDS_WEB_BROWSER FALSE)
juce_generate_juce_header(Onde)
# Page de la télécommande (téléphone), embarquée dans l'exécutable.
juce_add_binary_data(OndeData HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES Resources/remote.html Resources/logo.svg Resources/logo-192.png Resources/logo-32.png)
target_compile_features(Onde PRIVATE cxx_std_20)
target_sources(Onde PRIVATE
    Source/Main.cpp Source/MainComponent.cpp Source/SandboxPlugin.cpp Source/DemoContent.cpp
    Source/EditContext.cpp Source/PluginCatalog.cpp Source/ArrangementView.cpp Source/MixerView.cpp
    Source/PianoRollView.cpp Source/CommandPalette.cpp Source/Panels.cpp Source/AudioEditorView.cpp Source/Exporter.cpp Source/StemSeparator.cpp
    Source/QrCode.cpp Source/RemoteControl.cpp Source/RemotePanel.cpp Source/Logo.cpp Source/AboutPanel.cpp Source/HubPanel.cpp Source/HistoryPanel.cpp Source/ModulatorPanel.cpp Source/KeyboardPanel.cpp Source/OndeSynth.cpp Source/LibraryPanel.cpp Source/OndeFx2.cpp Source/Chains.cpp Source/Drummer.cpp Source/DrummerPanel.cpp Source/NewTrackPanel.cpp Source/Lyrics.cpp
    Source/OndeEffects.cpp Source/OndeEffectEditors.cpp)
target_compile_definitions(Onde PRIVATE ${ONDE_JUCE_DEFS})
if(MSVC)
    # Symboles publics (noms de fonctions) pour les diagnostics, sans changer le code généré.
    target_link_options(Onde PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()
target_link_libraries(Onde PRIVATE
    onde_sandbox OndeData tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
    juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Onde PRIVATE "-latomic")
endif()

# --- Séparateur de sources (Demucs v4 fine-tuned) : processus isolé -------------------------------
# Sources de demucs.cpp (MIT, Sevag Hanssian) copiées dans Worker/demucs et optimisées pour MSVC :
# normalisations, softmax et im2col réécrits avec des accès mémoire contigus (mêmes calculs).
# Seul Eigen est pris dans deps/demucs.cpp/vendor.
set(ONDE_DEMUCS_DIR "${ONDE_DEPS_DIR}/demucs.cpp" CACHE PATH "Chemin vers demucs.cpp (avec vendor/eigen)")
set(ONDE_DEMUCS_MODELS "${ONDE_DEPS_DIR}/demucs-models" CACHE PATH "Modèles htdemucs_ft (ggml f16)")
if(EXISTS "${ONDE_DEMUCS_DIR}/vendor/eigen/Eigen/Dense")
    file(GLOB ONDE_DEMUCS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Worker/demucs/*.cpp")
    add_executable(onde-stems Worker/StemsMain.cpp ${ONDE_DEMUCS_SOURCES})
    target_include_directories(onde-stems PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Worker/demucs" "${ONDE_DEMUCS_DIR}/vendor/eigen")
    target_compile_features(onde-stems PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(onde-stems PRIVATE /O2 /Ob3 /Oi /Ot /GS- /arch:AVX2 /fp:fast /bigobj /W0)
        target_compile_definitions(onde-stems PRIVATE NDEBUG _USE_MATH_DEFINES NOMINMAX)
        target_compile_options(onde-stems PRIVATE /Zi)
        target_link_options(onde-stems PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    else()
        target_compile_options(onde-stems PRIVATE -O3 -march=native -fno-unsafe-math-optimizations -freciprocal-math -fno-signed-zeros)
        target_compile_definitions(onde-stems PRIVATE NDEBUG)
    endif()
    set(ONDE_HAVE_STEMS TRUE)
endif()

# --- Plugins VST3 de test + test d'isolation VST3 ----------------------------------------
add_subdirectory(TestPlugins)
add_executable(onde-vst3-test Worker/VST3HostTest.cpp)
target_link_libraries(onde-vst3-test PRIVATE onde_sandbox)

# Le worker et les plugins de test sont copiés à côté d'Onde, à chaque compilation.
add_custom_target(onde-runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:onde-plugin-host> $<TARGET_FILE_DIR:Onde>/
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:Onde>/plugins-de-test"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Onde>/plugins-de-test"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<GENEX_EVAL:$<TARGET_PROPERTY:OndeTestSynth_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>" "$<TARGET_FILE_DIR:Onde>/plugins-de-test/Onde Test Synth.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<GENEX_EVAL:$<TARGET_PROPERTY:OndeTestCrash_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>" "$<TARGET_FILE_DIR:Onde>/plugins-de-test/Onde Test Crash.vst3"
    VERBATIM)
add_dependencies(onde-runtime Onde onde-plugin-host OndeTestSynth_VST3 OndeTestCrash_VST3)
if(ONDE_HAVE_STEMS)
    add_custom_command(TARGET onde-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:onde-stems> $<TARGET_FILE_DIR:Onde>/
        VERBATIM)
    add_dependencies(onde-runtime onde-stems)
    # Les modèles (≈ 340 Mo, hors dépôt) ne sont copiés que s'ils ont été téléchargés ; sans eux,
    # Onde indique que la séparation est indisponible.
    if(EXISTS "${ONDE_DEMUCS_MODELS}/ggml-model-htdemucs_ft_vocals-4s-f16.bin")
        add_custom_command(TARGET onde-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Onde>/modeles-demucs"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONDE_DEMUCS_MODELS}/ggml-model-htdemucs_ft_drums-4s-f16.bin" "${ONDE_DEMUCS_MODELS}/ggml-model-htdemucs_ft_bass-4s-f16.bin"
                "${ONDE_DEMUCS_MODELS}/ggml-model-htdemucs_ft_other-4s-f16.bin" "${ONDE_DEMUCS_MODELS}/ggml-model-htdemucs_ft_vocals-4s-f16.bin"
                "$<TARGET_FILE_DIR:Onde>/modeles-demucs/"
            VERBATIM)
    endif()
endif()
