# Plugins VST3 de test (synthé + effet qui plante sur demande), copiés à côté d'Onde.
foreach(kind Synth Crash)
    if(kind STREQUAL "Synth")
        set(code Ots1)
        set(synth TRUE)
        set(isSynthDef 1)
    else()
        set(code Otc1)
        set(synth FALSE)
        set(isSynthDef 0)
    endif()
    juce_add_plugin(OndeTest${kind}
        PRODUCT_NAME "Onde Test ${kind}"
        COMPANY_NAME "Onde"
        PLUGIN_MANUFACTURER_CODE Onde
        PLUGIN_CODE ${code}
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3
        COPY_PLUGIN_AFTER_BUILD FALSE)
    juce_generate_juce_header(OndeTest${kind})
    target_sources(OndeTest${kind} PRIVATE OndeTestPlugins.cpp)
    target_compile_definitions(OndeTest${kind} PUBLIC ONDE_TEST_SYNTH=${isSynthDef} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_compile_features(OndeTest${kind} PRIVATE cxx_std_20)
    target_link_libraries(OndeTest${kind} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    if(MINGW)
        target_link_options(OndeTest${kind} PRIVATE -static)
    endif()
endforeach()
