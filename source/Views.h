#pragma once

#include "Analyser.h"
#include "Style.h"

//==============================================================================
// Text helpers
//==============================================================================
namespace Plot
{
    inline juce::Font labelFont (float size = 10.0f)   { return juce::Font (juce::FontOptions (size)); }
    inline juce::Font boldFont (float size = 11.0f)    { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }

    // Small LED readout (as the value boxes of the knobs)
    inline void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour colour)
    {
        g.setColour (Colours::text);
        g.fillRect (r);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), r.getTopLeft(),
                                                 juce::Colours::transparentBlack, r.getTopLeft().translated (0.0f, 5.0f), false));
        g.fillRect (r.withHeight (5.0f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (r.getX(), r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f, 1.0f);
        SevenSegment::draw (g, r, text, colour);
    }

    inline juce::String signedValue (float v, int decimals)   { return (v >= 0.0f ? "+" : "") + juce::String (v, decimals); }
}

//==============================================================================
// A static, opaque layer (background, display, grid, labels) rendered once into an
// image at the physical pixel scale and only redrawn when the size or the scale changes;
// the render function must fill the whole area
//==============================================================================
class CachedLayer
{
public:
    template <typename Render>
    void draw (juce::Graphics& g, juce::Rectangle<int> bounds, Render&& render)
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::roundToInt ((float) bounds.getWidth() * scale), h = juce::roundToInt ((float) bounds.getHeight() * scale);
        if (w <= 0 || h <= 0)
            return;
        if (image.getWidth() != w || image.getHeight() != h || ! juce::approximatelyEqual (scale, imageScale))
        {
            image = juce::Image (juce::Image::RGB, w, h, false);   // opaque: drawn as a plain copy
            imageScale = scale;
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            render (ig);
        }
        drawAtScale (g, image, imageScale, bounds.getPosition().toFloat());
    }

    void clear()   { image = {}; }

    // An image made at 'scale' back 1:1 onto the physical pixels (no resampling)
    static void drawAtScale (juce::Graphics& g, const juce::Image& image, float scale, juce::Point<float> origin)
    {
        g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale).translated (origin));
    }

private:
    juce::Image image;
    float imageScale = 0.0f;
};

//==============================================================================
// Phase correlation over frequency: +1 (in phase) at the top, 0 in the middle, -1 (out
// of phase) at the bottom. Above zero the area is dark, below it red; quiet bands fade
// out. A faint spectrum lies behind, the dashed red line is the minimum since the last
// reset (click into the display).
//==============================================================================
class CorrelationView final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    std::function<void()> onReset;

    CorrelationView()   { setOpaque (true); }

    void setCurve (const Analysis::Curve& c, bool showHold)
    {
        curve = c;
        hold = showHold;
        repaint (plot.toNearestInt().expanded (2));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onReset != nullptr)
            onReset();
    }

    void resized() override
    {
        plot = getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (4.0f).withTrimmedBottom (16.0f).withTrimmedTop (2.0f);
        for (size_t p = 0; p < xs.size(); ++p)
            xs[p] = xOf (Analysis::frequencyOf ((int) p));
        layer.clear();
    }

    void paint (juce::Graphics& g) override
    {
        // Static part: background, display, grid and labels (cached)
        layer.draw (g, getLocalBounds(), [this] (juce::Graphics& lg) { drawStatic (lg); });
        if (! curve.valid)
            return;

        const float mid = plot.getCentreY();
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        // Spectrum behind (-100 ... 0 dB from the bottom), drawn through the midpoints
        {
            const auto point = [this] (size_t p)
            {
                const float level = juce::jlimit (0.0f, 1.0f, (curve.levelDb[p] + 100.0f) / 100.0f);
                return juce::Point<float> (xs[p], plot.getBottom() - level * plot.getHeight());
            };
            juce::Path area;
            area.startNewSubPath (plot.getBottomLeft());
            area.lineTo (point (0));
            for (size_t p = 1; p < (size_t) Analysis::numPoints; ++p)
            {
                const auto a = point (p - 1), b = point (p);
                area.quadraticTo (a, (a + b) * 0.5f);
            }
            area.lineTo (plot.getRight(), point ((size_t) Analysis::numPoints - 1).y);
            area.lineTo (plot.getBottomRight());
            area.closeSubPath();
            g.setColour (Colours::text.withAlpha (0.07f));
            g.fillPath (area);
        }

        // Correlation: one area above zero (dark) and one below (red), each a single shape
        // filled with a horizontal gradient that fades quiet bands; the line likewise, once
        // clipped to the upper and once to the lower half
        juce::Path upper, lower, line;
        upper.startNewSubPath (xs[0], mid);
        lower.startNewSubPath (xs[0], mid);
        for (size_t p = 0; p < (size_t) Analysis::numPoints; ++p)
        {
            const float v = curve.correlation[p];
            if (p > 0)
            {
                const float v0 = curve.correlation[p - 1];
                if ((v0 < 0.0f) != (v < 0.0f))   // crossing zero: both shapes meet there
                {
                    const float x = xs[p - 1] + (xs[p] - xs[p - 1]) * v0 / (v0 - v);
                    upper.lineTo (x, mid);
                    lower.lineTo (x, mid);
                }
            }
            const float y = yOf (v);
            upper.lineTo (xs[p], juce::jmin (y, mid));
            lower.lineTo (xs[p], juce::jmax (y, mid));
            if (p == 0) line.startNewSubPath (xs[p], y); else line.lineTo (xs[p], y);
        }
        for (auto* shape : { &upper, &lower })
        {
            shape->lineTo (xs[(size_t) Analysis::numPoints - 1], mid);
            shape->closeSubPath();
        }

        const auto gradient = [this] (juce::Colour colour, float alpha)
        {
            juce::ColourGradient grad (colour.withAlpha (0.0f), plot.getX(), 0.0f, colour.withAlpha (0.0f), plot.getRight(), 0.0f, false);
            grad.clearColours();
            for (size_t p = 0; p < (size_t) Analysis::numPoints; p += 2)
                grad.addColour (((double) xs[p] - (double) plot.getX()) / (double) plot.getWidth(),
                                colour.withAlpha (alpha * juce::jlimit (0.12f, 1.0f, (curve.levelDb[p] + 90.0f) / 40.0f)));
            return grad;
        };
        g.setGradientFill (gradient (Colours::text, 0.2f));
        g.fillPath (upper);
        g.setGradientFill (gradient (Colours::accent, 0.45f));
        g.fillPath (lower);

        const juce::PathStrokeType stroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        {
            juce::Graphics::ScopedSaveState half (g);
            g.reduceClipRegion (plot.withBottom (mid).toNearestInt());
            g.setGradientFill (gradient (Colours::text, 0.9f));
            g.strokePath (line, stroke);
        }
        {
            juce::Graphics::ScopedSaveState half (g);
            g.reduceClipRegion (plot.withTop (mid).toNearestInt());
            g.setGradientFill (gradient (Colours::accent, 1.0f));
            g.strokePath (line, stroke);
        }

        // Minimum since the last reset
        if (hold)
        {
            juce::Path minimum;
            minimum.startNewSubPath (xs[0], yOf (curve.minimum[0]));
            for (size_t p = 1; p < (size_t) Analysis::numPoints; ++p)
                minimum.lineTo (xs[p], yOf (curve.minimum[p]));
            const float dashes[] { 4.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, minimum, dashes, 2);
            g.setColour (Colours::accent.withAlpha (0.85f));
            g.fillPath (dashed);
        }
    }

private:
    float xOf (float hz) const   { return plot.getX() + plot.getWidth() * std::log (hz / Analysis::minHz) / std::log (Analysis::maxHz / Analysis::minHz); }
    float yOf (float r) const    { return plot.getCentreY() - r * plot.getHeight() * 0.46f; }

    void drawStatic (juce::Graphics& g) const
    {
        g.fillAll (Colours::background);
        Plot::drawDisplay (g, plot);
        g.setFont (Plot::labelFont());
        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xOf (hz);
            g.setColour (Colours::grid);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (Colours::text.withAlpha (0.6f));
            const bool last = hz > 15000.0f;   // the right edge: label left of the line
            g.drawText (hz < 1000.0f ? juce::String ((int) hz) : juce::String ((int) hz / 1000) + "k",
                        juce::Rectangle<float> (last ? x - 40.0f : x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f),
                        last ? juce::Justification::centredRight : juce::Justification::centred);
        }
        for (float r : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
        {
            const float y = yOf (r);
            g.setColour (juce::exactlyEqual (r, 0.0f) ? Colours::text.withAlpha (0.45f) : Colours::grid);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (Colours::text.withAlpha (0.6f));
            g.drawText (juce::exactlyEqual (r, 0.0f) ? juce::String ("0") : Plot::signedValue (r, juce::exactlyEqual (std::abs (r), 1.0f) ? 0 : 1),
                        juce::Rectangle<float> (plot.getX() - 34.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
        }
        g.setColour (Colours::text.withAlpha (0.55f));
        g.setFont (Plot::boldFont (10.0f));
        g.drawText ("IN PHASE", plot.reduced (8.0f, 6.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
        g.drawText ("OUT OF PHASE", plot.reduced (8.0f, 6.0f).removeFromBottom (12.0f), juce::Justification::centredLeft);
    }

    Analysis::Curve curve;
    bool hold = true;
    juce::Rectangle<float> plot;
    std::array<float, Analysis::numPoints> xs {};   // x of the display points
    CachedLayer layer;
};

//==============================================================================
// Broadband correlation: bar from the middle (0) to the value, with a readout
//==============================================================================
class CorrelationMeter final : public juce::Component
{
public:
    void setValue (float newValue)
    {
        if (std::abs (newValue - value) > 0.005f)
        {
            value = newValue;
            repaint();
        }
    }

    CorrelationMeter()   { setOpaque (true); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::background);
        auto r = getLocalBounds().toFloat();
        const auto readout = r.removeFromRight (80.0f).withSizeKeepingCentre (80.0f, 24.0f).translated (0.0f, -5.0f);
        r.removeFromRight (14.0f);
        r.removeFromLeft (34.0f);
        const auto bar = r.removeFromTop (r.getHeight() - 14.0f).reduced (0.0f, 2.0f);
        Plot::drawDisplay (g, bar);

        const auto xOf = [&bar] (float v) { return bar.getX() + bar.getWidth() * 0.5f * (v + 1.0f); };
        g.setFont (Plot::labelFont());
        for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        {
            g.setColour (juce::exactlyEqual (v, 0.0f) ? Colours::text.withAlpha (0.45f) : Colours::grid);
            g.drawVerticalLine (juce::roundToInt (xOf (v)), bar.getY(), bar.getBottom());
            g.setColour (Colours::text.withAlpha (0.6f));
            g.drawText (juce::exactlyEqual (v, 0.0f) ? juce::String ("0") : Plot::signedValue (v, juce::exactlyEqual (std::abs (v), 1.0f) ? 0 : 1),
                        juce::Rectangle<float> (xOf (v) - 20.0f, bar.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
        }

        const float x0 = xOf (0.0f), x1 = xOf (value);
        const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x1), bar.getY() + 5.0f, juce::jmax (x0, x1), bar.getBottom() - 5.0f);
        g.setColour (value < 0.0f ? Colours::accent : Colours::text.withAlpha (0.75f));
        g.fillRect (fill);
        g.fillRect (juce::Rectangle<float> (3.0f, bar.getHeight() - 4.0f).withCentre ({ x1, bar.getCentreY() }));

        Plot::drawReadout (g, readout, Plot::signedValue (value, 2), Colours::accent);
    }

private:
    float value = 1.0f;
};

//==============================================================================
// Vectorscopes. All three use mid / side with the stereo image upright:
//      x = (R - L) / sqrt 2  (left channel to the left),  y = (L + R) / sqrt 2
// so mono is the vertical line, a single channel lies on a diagonal and out of phase
// signal is horizontal. 'gain' scales so that 1 is the edge of the scope.
//==============================================================================
class Scope : public juce::Component
{
public:
    Scope()   { setOpaque (true); }

    // Static part (display and grid) from the cache, then the afterglow and the moving parts
    void paint (juce::Graphics& g) final
    {
        pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        layer.draw (g, getLocalBounds(), [this] (juce::Graphics& lg)
        {
            lg.fillAll (Colours::background);
            Plot::drawDisplay (lg, area());
            drawGrid (lg);
        });
        if (trail.isValid())
            CachedLayer::drawAtScale (g, trail, trailScale, {});
        drawMoving (g);
        paintGainText (g, area());
    }

    void resized() override   { layer.clear(); }

    // New samples (the newest at the end), gain in linear units, persistence 0 ... 1
    virtual void addSamples (const float* left, const float* right, int n, float gain, float persistence) = 0;

    void setGainText (const juce::String& text)
    {
        if (text != gainText)
        {
            gainText = text;
            repaint();
        }
    }

protected:
    static constexpr float sqrtHalf = 0.70710678f;

    juce::Rectangle<float> area() const   { return getLocalBounds().toFloat().reduced (1.0f); }

    virtual void drawGrid (juce::Graphics&) = 0;    // static, cached
    virtual void drawMoving (juce::Graphics&) {}     // every frame

    // The afterglow image (at the physical pixel scale of the last paint). A software image:
    // its pixels are written directly, which with a GPU-backed image (Direct2D on Windows)
    // would copy it from and to the graphics card every frame.
    juce::Image& trailImage (float persistence)
    {
        const int w = juce::roundToInt ((float) getWidth() * pixelScale), h = juce::roundToInt ((float) getHeight() * pixelScale);
        if (w <= 0 || h <= 0)
            return trail;
        if (trail.getWidth() != w || trail.getHeight() != h)
        {
            trail = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
            trailScale = pixelScale;
        }
        else if (persistence <= 0.0f)
            trail.clear (trail.getBounds());
        else
        {
            const float fade = 0.45f + 0.5f * persistence;   // per 1/60 s; the editor runs at 30 Hz
            trail.multiplyAllAlphas (fade * fade);
        }
        return trail;
    }

    void paintGainText (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::text.withAlpha (0.6f));
        g.setFont (Plot::boldFont (10.0f));
        g.drawText (gainText, r.reduced (7.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredRight);
    }

    // Dots into the image: (x, y) in pixels of the image
    static void plotDot (juce::Image::BitmapData& data, float x, float y, int size, juce::PixelARGB ink) noexcept
    {
        const int x0 = (int) x, y0 = (int) y;
        for (int dy = 0; dy < size; ++dy)
            for (int dx = 0; dx < size; ++dx)
            {
                const int px = x0 + dx, py = y0 + dy;
                if (px >= 0 && py >= 0 && px < data.width && py < data.height)
                    reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (px, py))->blend (ink);
            }
    }

    // Grid of a half disc (centre at the bottom): arcs and the rays L, M, R
    static void drawHalfDisc (juce::Graphics& g, juce::Point<float> centre, float radius, std::initializer_list<float> rings)
    {
        g.setColour (Colours::grid);
        for (float ring : rings)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius * ring, radius * ring, 0.0f,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (juce::exactlyEqual (ring, 1.0f) ? 1.2f : 0.8f));
        }
        g.drawLine (centre.x - radius, centre.y, centre.x + radius, centre.y, 1.0f);
        g.setFont (Plot::boldFont (10.0f));
        for (auto [angle, text] : { std::pair { -45.0f, "L" }, std::pair { 0.0f, "M" }, std::pair { 45.0f, "R" } })
        {
            const float a = juce::degreesToRadians (angle);
            const juce::Point<float> dir { std::sin (a), -std::cos (a) };
            g.setColour (juce::exactlyEqual (angle, 0.0f) ? Colours::text.withAlpha (0.3f) : Colours::grid);
            g.drawLine ({ centre, centre + dir * radius }, 1.0f);
            g.setColour (Colours::text.withAlpha (0.6f));
            g.drawText (text, juce::Rectangle<float> (16.0f, 12.0f).withCentre (centre + dir * (radius + 8.0f)), juce::Justification::centred);
        }
    }

    // Size of the half disc in 'r': centre at the bottom
    static std::pair<juce::Point<float>, float> halfDisc (juce::Rectangle<float> r)
    {
        const float radius = juce::jmax (10.0f, juce::jmin (r.getWidth() * 0.5f - 18.0f, r.getHeight() - 30.0f));
        return { { r.getCentreX(), r.getCentreY() + radius * 0.5f + 2.0f }, radius };
    }

    juce::Image trail;
    float pixelScale = 1.0f, trailScale = 1.0f;   // of the last paint / of the trail image
    juce::String gainText;
    CachedLayer layer;
};

//==============================================================================
// Polar sample: every sample as a dot; a sample and its negative mean the same stereo
// position, so the lower half is folded up (180 degrees view)
class PolarSampleScope final : public Scope
{
public:
    void addSamples (const float* left, const float* right, int n, float gain, float persistence) override
    {
        auto& image = trailImage (persistence);
        if (! image.isValid())
            return;
        const auto [centre, radius] = halfDisc (area());
        const float s = trailScale, r = radius * s * gain * sqrtHalf;
        const float cx = centre.x * s, cy = centre.y * s;
        const auto ink = Colours::text.withAlpha (0.5f).getPixelARGB();
        const int size = juce::jmax (1, juce::roundToInt (s * 1.5f));
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        for (int i = 0; i < n; ++i)
        {
            float x = right[i] - left[i], y = left[i] + right[i];
            if (y < 0.0f) { x = -x; y = -y; }   // fold: (L, R) and (-L, -R) are the same direction
            const float length = std::sqrt (x * x + y * y) * r;
            if (length > radius * s)   // beyond the edge: on the edge
            {
                const float k = radius * s / length;
                x *= k; y *= k;
            }
            plotDot (data, cx + x * r - 0.5f * (float) size, cy - y * r - 0.5f * (float) size, size, ink);
        }
        repaint();
    }

    void drawGrid (juce::Graphics& g) override
    {
        const auto [centre, radius] = halfDisc (area());
        drawHalfDisc (g, centre, radius, { 0.25f, 0.5f, 0.75f, 1.0f });
    }
};

//==============================================================================
// Polar level: the level per direction (2 degree sectors) as a shape from the centre,
// 48 dB from the centre to the edge; red marks: the peaks of the last 1.5 s
class PolarLevelScope final : public Scope
{
public:
    static constexpr int numSectors = 90;
    static constexpr float rangeDb = 48.0f;

    void addSamples (const float* left, const float* right, int n, float gain, float persistence) override
    {
        if (n <= 0)
            return;
        std::array<float, numSectors> power {};
        for (int i = 0; i < n; ++i)
        {
            float x = right[i] - left[i], y = left[i] + right[i];
            if (y < 0.0f) { x = -x; y = -y; }
            const float e = left[i] * left[i] + right[i] * right[i];
            if (e < 1.0e-12f)
                continue;
            const float angle = std::atan2 (x, y);   // -pi/2 ... pi/2
            const int sector = juce::jlimit (0, numSectors - 1, (int) ((angle / juce::MathConstants<float>::pi + 0.5f) * (float) numSectors));
            power[(size_t) sector] += e;
        }

        // Level per sector (relative to the gain), a little smoothed between neighbours,
        // fast up and falling with the persistence
        const float gainDb = juce::Decibels::gainToDecibels (gain);
        const float fall = 0.8f + 5.2f * (1.0f - persistence);   // dB per frame (30 Hz)
        for (size_t k = 0; k < (size_t) numSectors; ++k)
        {
            const float p = 0.25f * power[k == 0 ? k : k - 1] + 0.5f * power[k] + 0.25f * power[k + 1 < (size_t) numSectors ? k + 1 : k];
            const float db = 10.0f * std::log10 (p / (float) n + 1.0e-20f) + 2.0f * gainDb;
            levels[k] = juce::jmax (db, levels[k] - fall);
            if (levels[k] >= peaks[k]) { peaks[k] = levels[k]; holds[k] = 45; }
            else if (--holds[k] < 0)   peaks[k] -= 1.0f;
        }
        repaint();
    }

    void drawGrid (juce::Graphics& g) override
    {
        const auto [centre, radius] = halfDisc (area());
        drawHalfDisc (g, centre, radius, { 0.25f, 0.5f, 0.75f, 1.0f });
        g.setColour (Colours::text.withAlpha (0.5f));
        g.setFont (Plot::labelFont (9.0f));
        for (float ring : { 0.25f, 0.5f, 0.75f })
            g.drawText (juce::String (-juce::roundToInt ((1.0f - ring) * rangeDb)),
                        juce::Rectangle<float> (24.0f, 10.0f).withCentre ({ centre.x + radius * ring - 10.0f, centre.y - 6.0f }), juce::Justification::centred);
    }

    void drawMoving (juce::Graphics& g) override
    {
        const auto [centre, radius] = halfDisc (area());
        const auto pointOf = [&, c = centre, rad = radius] (size_t k, float db)
        {
            const float a = ((float) k + 0.5f) / (float) numSectors * juce::MathConstants<float>::pi - juce::MathConstants<float>::halfPi;
            const float length = juce::jlimit (0.0f, 1.0f, (db + rangeDb) / rangeDb) * rad;
            return c + juce::Point<float> (std::sin (a), -std::cos (a)) * length;
        };
        juce::Path shape;
        shape.startNewSubPath (centre);
        for (size_t k = 0; k < (size_t) numSectors; ++k)
            shape.lineTo (pointOf (k, levels[k]));
        shape.closeSubPath();
        g.setColour (Colours::text.withAlpha (0.35f));
        g.fillPath (shape);
        g.setColour (Colours::text.withAlpha (0.85f));
        g.strokePath (shape, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));

        juce::Path peakDots;
        for (size_t k = 0; k < (size_t) numSectors; ++k)
            if (peaks[k] > -rangeDb)
                peakDots.addEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre (pointOf (k, peaks[k])));
        g.setColour (Colours::accent);
        g.fillPath (peakDots);
    }

private:
    std::array<float, numSectors> levels = makeFilled (-200.0f), peaks = makeFilled (-200.0f);
    std::array<int, numSectors> holds {};

    static std::array<float, numSectors> makeFilled (float v)   { std::array<float, numSectors> a; a.fill (v); return a; }
};

//==============================================================================
// Lissajous (goniometer): the samples joined by lines, full plane
class LissajousScope final : public Scope
{
public:
    void addSamples (const float* left, const float* right, int n, float gain, float persistence) override
    {
        auto& image = trailImage (persistence);
        if (! image.isValid() || n <= 0)
            return;
        const auto [centre, radius] = square (area());
        const float s = trailScale, r = radius * s * gain * sqrtHalf;
        const float cx = centre.x * s, cy = centre.y * s, edge = radius * s;
        const auto pointOf = [&] (int i)
        {
            return juce::Point<float> (cx + juce::jlimit (-edge, edge, (right[i] - left[i]) * r),
                                       cy - juce::jlimit (-edge, edge, (left[i] + right[i]) * r));
        };

        // The samples joined: dots every pixel along each line (direct pixel writes)
        const auto ink = Colours::text.withAlpha (0.4f).getPixelARGB();
        const int size = juce::jmax (1, juce::roundToInt (s));
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        auto from = hasLast ? last : pointOf (0);
        for (int i = 0; i < n; ++i)
        {
            const auto to = pointOf (i);
            const int steps = juce::jlimit (1, 256, (int) (juce::jmax (std::abs (to.x - from.x), std::abs (to.y - from.y)) / (float) size));
            for (int k = 1; k <= steps; ++k)
            {
                const auto p = from + (to - from) * ((float) k / (float) steps);
                plotDot (data, p.x, p.y, size, ink);
            }
            from = to;
        }
        last = from;
        hasLast = true;
        repaint();
    }

    void resized() override
    {
        Scope::resized();
        hasLast = false;
    }

    void drawGrid (juce::Graphics& g) override
    {
        const auto [centre, radius] = square (area());

        g.setColour (Colours::grid);
        juce::Path diamond;   // the edge for the full scale of one channel
        diamond.addQuadrilateral (centre.x, centre.y - radius, centre.x + radius, centre.y, centre.x, centre.y + radius, centre.x - radius, centre.y);
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
        g.drawLine (centre.x - radius, centre.y, centre.x + radius, centre.y, 1.0f);
        const float d = radius * sqrtHalf;
        g.drawLine (centre.x - d, centre.y - d, centre.x + d, centre.y + d, 1.0f);
        g.drawLine (centre.x + d, centre.y - d, centre.x - d, centre.y + d, 1.0f);
        g.setColour (Colours::text.withAlpha (0.3f));
        g.drawLine (centre.x, centre.y - radius, centre.x, centre.y + radius, 1.0f);

        g.setColour (Colours::text.withAlpha (0.6f));
        g.setFont (Plot::boldFont (10.0f));
        const auto label = [&g] (const char* text, juce::Point<float> at) { g.drawText (text, juce::Rectangle<float> (16.0f, 12.0f).withCentre (at), juce::Justification::centred); };
        label ("M", { centre.x, centre.y - radius - 8.0f });
        label ("L", { centre.x - d - 7.0f, centre.y - d - 7.0f });
        label ("R", { centre.x + d + 7.0f, centre.y - d - 7.0f });
        label ("S", { centre.x - radius - 9.0f, centre.y });
        label ("S", { centre.x + radius + 9.0f, centre.y });

    }

private:
    static std::pair<juce::Point<float>, float> square (juce::Rectangle<float> r)
    {
        return { r.getCentre(), juce::jmax (10.0f, 0.5f * juce::jmin (r.getWidth(), r.getHeight()) - 18.0f) };
    }

    juce::Point<float> last;
    bool hasLast = false;
};
