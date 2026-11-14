#pragma once

#include <juce_core/juce_core.h>

// Les textes accentués doivent être décodés explicitement en UTF-8 : le
// compilateur MSVC lit les sources en UTF-8 (option /utf-8), mais les chaînes
// littérales restent des octets. juce::String::fromUTF8 fait la conversion.
// Ce petit fichier est partagé par le plugin de piste et Rogus Master.
namespace rogus
{
    inline juce::String fr (const char* texte) { return juce::String::fromUTF8 (texte); }
}
