#pragma once

#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Phase correlation over frequency (runs on the message thread).
//
// Short-time FFT of both channels (Hann window, hop of 1/4 frame). Per bin the cross
// power Re(L conj R) and the powers |L|^2, |R|^2 are averaged in time; per display point
// they are summed over a band of 1/3 ... 1/24 octave (prefix sums) and the correlation is
//      r = sum Re(L conj R) / sqrt (sum |L|^2 * sum |R|^2)        (-1 ... +1)
// The same averages give the level of the band (for the faint spectrum behind it).
//==============================================================================
namespace Analysis
{
    constexpr int numPoints = 320;               // display points, log spaced
    constexpr float minHz = 20.0f, maxHz = 20000.0f;

    inline float frequencyOf (int point) noexcept
    {
        return minHz * std::pow (maxHz / minHz, (float) point / (float) (numPoints - 1));
    }

    struct Curve
    {
        std::array<float, numPoints> correlation {}, levelDb {}, minimum {};
        bool valid = false;
    };

    class Correlation
    {
    public:
        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;
            const int order = 13 + (sampleRate > 60000.0 ? 1 : 0) + (sampleRate > 120000.0 ? 1 : 0);   // about 6 Hz bins
            fft = std::make_unique<juce::dsp::FFT> (order);
            size = 1 << order;
            hop = size / 4;   // 75 % overlap
            bins = size / 2 + 1;

            window.resize ((size_t) size);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) size,
                                                                      juce::dsp::WindowingFunction<float>::hann, false);
            float sum = 0.0f;
            for (float w : window) sum += w;
            levelScale = 4.0f / (sum * sum);   // a full scale sine: about 0 dB in its band

            for (auto* v : { &historyL, &historyR }) v->assign ((size_t) size, 0.0f);
            for (auto* v : { &fftL, &fftR }) v->assign ((size_t) size * 2, 0.0f);
            for (auto* v : { &cross, &powerL, &powerR }) v->assign ((size_t) bins, 0.0f);
            for (auto* v : { &sumCross, &sumL, &sumR }) v->assign ((size_t) bins + 1, 0.0);
            filled = 0;
            sinceFrame = 0;
            frames = 0;
            bandsFor = -1.0f;
            resetMinimum();
        }

        double getSampleRate() const noexcept   { return sampleRate; }

        // New samples; returns true when at least one frame was analysed
        bool push (const float* left, const float* right, int n, float averageSeconds)
        {
            bool analysed = false;
            const float coefficient = 1.0f - std::exp (-(float) hop / (float) (sampleRate * averageSeconds));
            for (int i = 0; i < n; ++i)
            {
                historyL[(size_t) writePos] = left[i];
                historyR[(size_t) writePos] = right[i];
                writePos = (writePos + 1) % size;
                filled = juce::jmin (size, filled + 1);
                if (++sinceFrame >= hop && filled == size)
                {
                    sinceFrame = 0;
                    analyseFrame (frames++ == 0 ? 1.0f : coefficient);
                    analysed = true;
                }
            }
            return analysed;
        }

        // The curve for a smoothing of 'octaves' (band width)
        void evaluate (Curve& c, float octaves)
        {
            if (frames == 0)
                return;
            if (! juce::exactlyEqual (octaves, bandsFor))
                makeBands (octaves);

            for (int k = 0; k < bins; ++k)   // prefix sums: any band in O(1)
            {
                sumCross[(size_t) k + 1] = sumCross[(size_t) k] + cross[(size_t) k];
                sumL[(size_t) k + 1]     = sumL[(size_t) k]     + powerL[(size_t) k];
                sumR[(size_t) k + 1]     = sumR[(size_t) k]     + powerR[(size_t) k];
            }
            for (size_t p = 0; p < (size_t) numPoints; ++p)
            {
                const auto [lo, hi] = bands[p];
                const double xy = sumCross[(size_t) hi] - sumCross[(size_t) lo];
                const double xx = sumL[(size_t) hi] - sumL[(size_t) lo];
                const double yy = sumR[(size_t) hi] - sumR[(size_t) lo];
                const double power = 0.5 * (xx + yy);
                c.levelDb[p] = 0.5f * juce::Decibels::gainToDecibels ((float) (power * levelScale), -300.0f);   // power in dB
                const bool audible = c.levelDb[p] > silenceDb;
                c.correlation[p] = audible ? (float) juce::jlimit (-1.0, 1.0, xy / std::sqrt (xx * yy + 1.0e-30)) : 1.0f;
                if (audible)
                    minimum[p] = juce::jmin (minimum[p], c.correlation[p]);
                c.minimum[p] = minimum[p];
            }
            c.valid = true;
        }

        void resetMinimum() noexcept   { minimum.fill (1.0f); }

        static constexpr float silenceDb = -100.0f;   // quieter bands count as correlated (nothing to see)

    private:
        void analyseFrame (float coefficient)
        {
            for (int i = 0; i < size; ++i)   // oldest sample first
            {
                const auto from = (size_t) ((writePos + i) % size);
                fftL[(size_t) i] = historyL[from] * window[(size_t) i];
                fftR[(size_t) i] = historyR[from] * window[(size_t) i];
            }
            fft->performRealOnlyForwardTransform (fftL.data(), true);
            fft->performRealOnlyForwardTransform (fftR.data(), true);

            for (size_t k = 0; k < (size_t) bins; ++k)
            {
                const float lr = fftL[2 * k], li = fftL[2 * k + 1], rr = fftR[2 * k], ri = fftR[2 * k + 1];
                cross[k]  += coefficient * (lr * rr + li * ri - cross[k]);
                powerL[k] += coefficient * (lr * lr + li * li - powerL[k]);
                powerR[k] += coefficient * (rr * rr + ri * ri - powerR[k]);
            }
        }

        void makeBands (float octaves)
        {
            bandsFor = octaves;
            const float binHz = (float) sampleRate / (float) size, half = std::exp2 (0.5f * octaves);
            for (size_t p = 0; p < (size_t) numPoints; ++p)
            {
                const float f = frequencyOf ((int) p);
                int lo = juce::roundToInt (f / half / binHz), hi = juce::roundToInt (f * half / binHz) + 1;
                lo = juce::jlimit (1, bins - 1, lo);
                hi = juce::jlimit (lo + 1, bins, hi);
                bands[p] = { lo, hi };
            }
        }

        double sampleRate = 48000.0;
        std::unique_ptr<juce::dsp::FFT> fft;
        int size = 0, hop = 0, bins = 0, writePos = 0, filled = 0, sinceFrame = 0;
        long long frames = 0;
        float levelScale = 1.0f, bandsFor = -1.0f;
        std::vector<float> window, historyL, historyR, fftL, fftR, cross, powerL, powerR;
        std::vector<double> sumCross, sumL, sumR;
        std::array<std::pair<int, int>, numPoints> bands {};
        std::array<float, numPoints> minimum {};
    };

    //==========================================================================
    // Broadband correlation and the level for the auto gain of the scopes
    class Broadband
    {
    public:
        void push (const float* left, const float* right, int n, float averageSeconds, double sampleRate) noexcept
        {
            const float a = 1.0f - std::exp (-1.0f / (float) (sampleRate * averageSeconds));
            const float peakFall = std::exp (-1.0f / (float) (sampleRate * 1.5));   // auto gain: peak falls with 1.5 s
            for (int i = 0; i < n; ++i)
            {
                const float l = left[i], r = right[i];
                xy += a * (l * r - xy);
                xx += a * (l * l - xx);
                yy += a * (r * r - yy);
                peak = juce::jmax (std::sqrt (l * l + r * r), peak * peakFall);
            }
        }

        float correlation() const noexcept   { return xx * yy > 1.0e-14f ? juce::jlimit (-1.0f, 1.0f, xy / std::sqrt (xx * yy)) : 1.0f; }
        float getPeak() const noexcept       { return peak; }

    private:
        float xy = 0.0f, xx = 0.0f, yy = 0.0f, peak = 0.0f;
    };
}
