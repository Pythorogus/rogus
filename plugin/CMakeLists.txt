# Plugin VST3 « Rogus » : assistant de mix, une instance par piste.
# Étape 1 : squelette minimal (coupe-bas) qui se compile et se charge dans FL Studio.

cmake_minimum_required(VERSION 3.22)

project(RogusPlugin VERSION 0.6.0 LANGUAGES C CXX)

# Numéro de version commun aux deux plugins, affiché en haut à droite de leur
# fenêtre (« v0.4 »). Deux chiffres suffisent : on augmente le second à chaque
# modification installée, en changeant la ligne project(...) ci-dessus. Il sert
# à vérifier d'un coup d'œil que FL Studio a bien chargé la dernière version.
set(ROGUS_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE est téléchargé à la première configuration, dans le dossier de build.
# La version est figée : une mise à jour de JUCE ne doit pas changer le son
# ou casser la compilation sans qu'on l'ait décidé.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Polices des fenêtres (Inter et JetBrains Mono, licence libre OFL, voir les
# fichiers de licence dans Polices/). Elles sont embarquées dans chaque plugin :
# la fenêtre a le même aspect sur tous les postes, sans rien installer.
juce_add_binary_data(RogusPolices
    HEADER_NAME "Polices.h"
    NAMESPACE   Polices
    SOURCES
        Polices/Inter-Regular.ttf
        Polices/Inter-SemiBold.ttf
        Polices/Inter-Bold.ttf
        Polices/JetBrainsMono-Regular.ttf
        Polices/JetBrainsMono-Bold.ttf
)

juce_add_plugin(Rogus
    PRODUCT_NAME              "Rogus"
    COMPANY_NAME              "Rogus"
    # Ces deux codes identifient le plugin auprès de l'hôte. Ne plus les changer :
    # FL Studio retrouve les réglages des projets enregistrés grâce à eux.
    PLUGIN_MANUFACTURER_CODE  Rogs
    PLUGIN_CODE               Rgs1
    FORMATS                   VST3
    VST3_CATEGORIES           Fx EQ
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    # La copie vers « C:\Program Files\Common Files\VST3 » demande les droits
    # administrateur : elle est faite à part, par installer.ps1, qui les demande.
    COPY_PLUGIN_AFTER_BUILD   FALSE
)

set(SOURCES_ROGUS
    Source/Correction.cpp
    Source/Orchestre.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Habillage.cpp
)

target_sources(Rogus PRIVATE ${SOURCES_ROGUS})

# Programme de tests : le même code que le plugin, lancé sans hôte sur des
# signaux de synthèse (voir tester.ps1).
juce_add_console_app(RogusTests PRODUCT_NAME "RogusTests")
target_sources(RogusTests PRIVATE Tests/tests.cpp ${SOURCES_ROGUS})
target_compile_definitions(RogusTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ROGUS_VERSION="${ROGUS_VERSION}")
target_compile_options(RogusTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(RogusTests
    PRIVATE
        RogusPolices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Essai sur de vrais fichiers WAV, hors FL Studio (voir Outils/essai.cpp) :
# un kick et une basse passent dans deux instances du moteur, comme dans l'hôte.
juce_add_console_app(RogusEssai PRODUCT_NAME "RogusEssai")
target_sources(RogusEssai PRIVATE Outils/essai.cpp ${SOURCES_ROGUS})
target_compile_definitions(RogusEssai PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ROGUS_VERSION="${ROGUS_VERSION}")
target_compile_options(RogusEssai PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(RogusEssai
    PRIVATE
        RogusPolices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(Rogus PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ROGUS_VERSION="${ROGUS_VERSION}"
)

# Les sources sont en UTF-8 (commentaires et textes en français). Sans cette
# option, le compilateur de Microsoft les lit dans l'encodage de Windows
# et abîme les lettres accentuées.
target_compile_options(Rogus PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(Rogus
    PRIVATE
        RogusPolices
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Rogus Master -----------------------------------------------------------
# Plugin séparé, posé sur la piste master. Il ne partage aucune mémoire avec
# les instances de Rogus : il n'a besoin que du signal du bus master.
juce_add_plugin(RogusMaster
    PRODUCT_NAME              "Rogus Master"
    COMPANY_NAME              "Rogus"
    PLUGIN_MANUFACTURER_CODE  Rogs
    PLUGIN_CODE               Rgs3
    FORMATS                   VST3
    VST3_CATEGORIES           Fx
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
)

set(SOURCES_MASTER
    SourceMaster/Limiteur.cpp
    SourceMaster/MesureLoudness.cpp
    SourceMaster/MasterProcessor.cpp
    SourceMaster/MasterEditor.cpp
    Source/Habillage.cpp
)

target_sources(RogusMaster PRIVATE ${SOURCES_MASTER})
target_include_directories(RogusMaster PRIVATE Source)

target_compile_definitions(RogusMaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ROGUS_VERSION="${ROGUS_VERSION}"
)

target_compile_options(RogusMaster PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(RogusMaster
    PRIVATE
        RogusPolices
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Tests de Rogus Master, lancés sans hôte sur des signaux de synthèse.
juce_add_console_app(RogusMasterTests PRODUCT_NAME "RogusMasterTests")
target_sources(RogusMasterTests PRIVATE TestsMaster/tests.cpp ${SOURCES_MASTER})
target_include_directories(RogusMasterTests PRIVATE Source SourceMaster)
target_compile_definitions(RogusMasterTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ROGUS_VERSION="${ROGUS_VERSION}")
target_compile_options(RogusMasterTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(RogusMasterTests
    PRIVATE
        RogusPolices
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
