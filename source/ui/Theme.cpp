#include "Theme.h"

#include "BinaryData.h"

namespace rain::ui
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
    }

    struct Typefaces
    {
        juce::Typeface::Ptr serifMedium = load (BinaryData::CormorantGaramondMedium_ttf, BinaryData::CormorantGaramondMedium_ttfSize);
        juce::Typeface::Ptr serifSemiBold = load (BinaryData::CormorantGaramondSemiBold_ttf, BinaryData::CormorantGaramondSemiBold_ttfSize);
        juce::Typeface::Ptr sansRegular = load (BinaryData::JostRegular_ttf, BinaryData::JostRegular_ttfSize);
        juce::Typeface::Ptr sansMedium = load (BinaryData::JostMedium_ttf, BinaryData::JostMedium_ttfSize);
    };

    const Typefaces& typefaces()
    {
        static const Typefaces t;
        return t;
    }
}

juce::Font Fonts::serif (float height, bool semiBold)
{
    const auto& t = typefaces();
    return juce::Font (juce::FontOptions (semiBold ? t.serifSemiBold : t.serifMedium).withHeight (height));
}

juce::Font Fonts::sans (float height, bool medium)
{
    const auto& t = typefaces();
    return juce::Font (juce::FontOptions (medium ? t.sansMedium : t.sansRegular).withHeight (height));
}

juce::Font Fonts::caps (float height, float tracking)
{
    return sans (height).withExtraKerningFactor (tracking);
}

} // namespace rain::ui
