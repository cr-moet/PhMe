#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PhMeLookAndFeel.h"

//==============================================================================
// Front panel drawing (the same look as UlMaTo): raised panels with screws and the
// grey displays set into the front
//==============================================================================
namespace Plot
{
    // Raised panel (the boxes the knobs sit on): soft shadow down right, slightly rounded
    // corners, light upper / left edge and darker lower / right edge
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        constexpr float corner = 4.0f;
        for (int i = 1; i <= 4; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.045f));
            g.fillRoundedRectangle (r.translated (1.0f + 0.5f * (float) i, 1.5f + 0.75f * (float) i).expanded ((float) i * 0.5f), corner + (float) i * 0.5f);
        }
        g.setGradientFill (juce::ColourGradient (Colours::background.darker (0.025f), r.getTopLeft(),   // just a shade darker than the front
                                                 Colours::background.darker (0.06f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, corner);

        juce::Path lit, shaded;   // edges: light from the top left
        lit.startNewSubPath (r.getX() + 0.5f, r.getBottom() - corner);
        lit.lineTo (r.getX() + 0.5f, r.getY() + corner);
        lit.quadraticTo (r.getX() + 0.5f, r.getY() + 0.5f, r.getX() + corner, r.getY() + 0.5f);
        lit.lineTo (r.getRight() - corner, r.getY() + 0.5f);
        shaded.startNewSubPath (r.getRight() - 0.5f, r.getY() + corner);
        shaded.lineTo (r.getRight() - 0.5f, r.getBottom() - corner);
        shaded.quadraticTo (r.getRight() - 0.5f, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f);
        shaded.lineTo (r.getX() + corner, r.getBottom() - 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.strokePath (lit, juce::PathStrokeType (1.2f));
        g.setColour (Colours::text.withAlpha (0.35f));
        g.strokePath (shaded, juce::PathStrokeType (1.2f));
        g.setColour (Colours::text.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.0f), corner, 0.8f);
    }

    // Faint spectrum behind a graph (mid of the processed signal): smoothed over half an

    // Button head screw with hexagon socket (black, slightly glossy dome); rotation turns the
    // socket so the screws do not all look the same
    inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float radius, float rotation)
    {
        const auto circle = [c] (float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };
        g.setColour (juce::Colours::black.withAlpha (0.25f));   // shadow and the recess around the head
        g.fillEllipse (circle (radius + 1.0f).translated (0.8f, 1.2f));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (circle (radius + 0.6f), 0.8f);

        juce::ColourGradient dome (juce::Colour (0xff5a5a5a), c.translated (-radius * 0.45f, -radius * 0.5f),
                                   juce::Colour (0xff101010), c.translated (radius * 0.7f, radius * 0.8f), true);
        dome.addColour (0.45, juce::Colour (0xff262626));
        g.setGradientFill (dome);
        g.fillEllipse (circle (radius));
        g.setColour (juce::Colours::white.withAlpha (0.12f));   // narrow flat rim of the head
        g.drawEllipse (circle (radius - 0.6f), 0.7f);

        // Hexagon socket: dark, the upper left inner walls in shadow, a light lower right edge
        const float hexR = radius * 0.42f;
        juce::Path hex;
        for (int i = 0; i < 6; ++i)
        {
            const float a = rotation + juce::MathConstants<float>::twoPi * (float) i / 6.0f;
            const auto pt = c + juce::Point<float> (std::cos (a), std::sin (a)) * hexR;
            if (i == 0) hex.startNewSubPath (pt); else hex.lineTo (pt);
        }
        hex.closeSubPath();
        g.setColour (juce::Colour (0xff050505));
        g.fillPath (hex);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (hex);
            juce::Path lit;
            lit.addRectangle (circle (radius));
            lit.addPath (hex, juce::AffineTransform::translation (-0.8f, -0.8f));
            lit.setUsingNonZeroWinding (false);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillPath (lit);
        }
        g.setColour (juce::Colours::white.withAlpha (0.35f));   // highlight on the dome
        g.fillEllipse (juce::Rectangle<float> (radius * 0.55f, radius * 0.35f).withCentre (c.translated (-radius * 0.42f, -radius * 0.55f)));
    }

    // Raised panel screwed onto the front at its four corners
    inline void drawScrewedPanel (juce::Graphics& g, juce::Rectangle<float> r, float screwRadius = 6.5f)
    {
        drawPanel (g, r);
        const float inset = screwRadius + 5.0f;
        int n = 0;
        for (auto c : { r.getTopLeft().translated (inset, inset), r.getTopRight().translated (-inset, inset),
                        r.getBottomLeft().translated (inset, -inset), r.getBottomRight().translated (-inset, -inset) })
        {
            const float rotation = 0.37f * (float) (n++) + 0.011f * (r.getX() + 3.0f * r.getY());
            drawScrew (g, c, screwRadius, rotation);
        }
    }

    // Graphic display: grey screen set into the front panel - dark edge and inner shadow at
    // the top and left, a light edge at the bottom and right
    inline void drawDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::display);
        g.fillRect (r);

        const auto shade = [&g] (juce::Rectangle<float> band, juce::Point<float> from, juce::Point<float> to, float alpha)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (alpha), from, juce::Colours::transparentBlack, to, false));
            g.fillRect (band);
        };
        const float top = juce::jmin (10.0f, r.getHeight() * 0.25f), left = juce::jmin (7.0f, r.getWidth() * 0.25f);
        shade (r.withHeight (top), r.getTopLeft(), r.getTopLeft().translated (0.0f, top), 0.38f);
        shade (r.withWidth (left), r.getTopLeft(), r.getTopLeft().translated (left, 0.0f), 0.25f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));             // edge of the cut-out
        g.drawLine (r.getX() - 0.5f, r.getY() - 0.5f, r.getRight() + 0.5f, r.getY() - 0.5f, 1.5f);
        g.drawLine (r.getX() - 0.5f, r.getY() - 0.5f, r.getX() - 0.5f, r.getBottom() + 0.5f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawLine (r.getX() - 1.0f, r.getBottom() + 1.0f, r.getRight() + 1.0f, r.getBottom() + 1.0f, 1.5f);
        g.drawLine (r.getRight() + 1.0f, r.getY() - 1.0f, r.getRight() + 1.0f, r.getBottom() + 1.0f, 1.5f);
    }

}
