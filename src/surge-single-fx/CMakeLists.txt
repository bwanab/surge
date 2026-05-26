# vi:set sw=2 et:

# Add one LV2/VST3/CLAP plugin per Surge XT effect type.
#
# Usage:
#   surge_add_single_fx(
#     short_name       # e.g. "chorus"
#     fx_type_int      # integer value of the fx_type enum, e.g. 9 for fxt_chorus4
#     display_name     # shown as plugin name, e.g. "Chorus"
#     lv2_uri_suffix   # appended to base URI, e.g. "surge-xt-chorus"
#     plugin_code      # 4-char VST3 manufacturer code
#     clap_id_suffix   # appended to CLAP org prefix, e.g. "surge-xt-chorus"
#   )

function(surge_add_single_fx short_name fx_type_int display_name lv2_uri_suffix plugin_code clap_id_suffix)
  set(target_name surge-single-fx-${short_name})

  juce_add_plugin(${target_name}
    PRODUCT_NAME "Surge XT ${display_name}"
    COMPANY_NAME "Surge Synth Team"
    COMPANY_WEBSITE "https://surge-synth-team.org/"
    BUNDLE_ID "org.surge-synth-team.${clap_id_suffix}"
    PLUGIN_MANUFACTURER_CODE VmbA
    PLUGIN_CODE ${plugin_code}

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    VST3_CATEGORIES Fx
    VST3_AUTO_MANIFEST FALSE

    LV2_URI https://surge-synth-team.org/plugins/${lv2_uri_suffix}
    LV2_SHARED_LIBRARY_NAME SurgeXT_${display_name}

    FORMATS ${SURGE_JUCE_FORMATS}
  )

  target_sources(${target_name} PRIVATE
    SingleFXProcessor.cpp
    SingleFXProcessor.h
  )

  target_compile_definitions(${target_name} PRIVATE
    SURGE_SINGLE_FX_TYPE=${fx_type_int}
    SURGE_SINGLE_FX_NAME="${display_name}"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
  )

  target_link_libraries(${target_name} PRIVATE
    surge::surge-common
    surge-juce
    surge-platform
    juce::juce_audio_utils
    juce::juce_audio_processors
  )

  if(UNIX AND NOT APPLE)
    target_link_options(${target_name}_LV2 PRIVATE -Wl,--exclude-libs,ALL)
  endif()

  surge_juce_package(${target_name} "Surge XT ${display_name}")
endfunction()

# --- Effect plugins ---
# fx_type integers from the fx_type enum in SurgeStorage.h
# fxt_delay = 1, fxt_reverb = 2, fxt_phaser = 3, fxt_rotaryspeaker = 4,
# fxt_chorus4 = 9, fxt_flanger = 12, fxt_ensemble = 20

surge_add_single_fx(chorus        9  "Chorus"         "surge-xt-chorus"         "S1CH" "surge-xt-chorus")
surge_add_single_fx(rotary        4  "Rotary Speaker" "surge-xt-rotary"         "S1RT" "surge-xt-rotary")
surge_add_single_fx(reverb1       2  "Reverb 1"       "surge-xt-reverb1"        "S1R1" "surge-xt-reverb1")
surge_add_single_fx(phaser        3  "Phaser"         "surge-xt-phaser"         "S1PH" "surge-xt-phaser")
surge_add_single_fx(ensemble     20  "Ensemble"       "surge-xt-ensemble"       "S1EN" "surge-xt-ensemble")
surge_add_single_fx(flanger      12  "Flanger"        "surge-xt-flanger"        "S1FL" "surge-xt-flanger")
surge_add_single_fx(delay         1  "Delay"          "surge-xt-delay"          "S1DL" "surge-xt-delay")
