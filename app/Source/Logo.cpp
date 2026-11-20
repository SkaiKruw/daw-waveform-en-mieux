#include "Theme.h"
#include "BinaryData.h"

namespace theme
{
    std::unique_ptr<juce::Drawable> makeLogo()
    {
        return juce::Drawable::createFromSVGString (juce::String::fromUTF8 (BinaryData::logo_svg, BinaryData::logo_svgSize));
    }

    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        static std::unique_ptr<juce::Drawable> logo = makeLogo();   // analysé une seule fois
        if (logo != nullptr)
            logo->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
    }
}
