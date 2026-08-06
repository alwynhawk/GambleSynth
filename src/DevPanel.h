#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"

// Hidden developer panel — unlocked by typing seed 777. Exposes every patch
// parameter as a live slider so the engine can be tuned by ear, which is much
// faster than editing the archetype ranges and re-rolling. Edits bypass the
// roll cut (see applyLiveEdit) so dragging doesn't mute the output.
class DevPanel : public juce::Component
{
public:
    explicit DevPanel (GambleSynthProcessor& p) : proc (p)
    {
        buildParams();

        for (auto& desc : params)
        {
            auto row = std::make_unique<Row>();

            row->label.setText (desc.name, juce::dontSendNotification);
            row->label.setColour (juce::Label::textColourId, Theme::ink());
            row->label.setFont (Theme::mono (11.0f));
            content.addAndMakeVisible (row->label);

            row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
            row->slider.setRange (desc.min, desc.max, desc.step);
            if (desc.skewMid > 0.0)
                row->slider.setSkewFactorFromMidPoint (desc.skewMid);
            row->slider.setColour (juce::Slider::trackColourId,          Theme::ink());
            row->slider.setColour (juce::Slider::thumbColourId,          Theme::ink());
            row->slider.setColour (juce::Slider::textBoxTextColourId,    Theme::ink());
            row->slider.setColour (juce::Slider::textBoxBackgroundColourId, Theme::ground());
            row->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

            const auto index = rows.size();
            row->slider.onValueChange = [this, index]
            {
                if (updating) return;
                Patch edited = proc.getPatch();
                params[index].set (edited, (float) rows[index]->slider.getValue());
                proc.applyLiveEdit (edited);
            };

            content.addAndMakeVisible (row->slider);
            rows.push_back (std::move (row));
        }

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        resetButton.setButtonText ("ROLL");
        resetButton.onClick = [this] { proc.pullLever(); };
        addAndMakeVisible (resetButton);

        syncFromPatch();
    }

    int getNumParams() const { return (int) params.size(); }

    // Pull slider positions back from the patch (after a roll, undo, load...).
    void syncFromPatch()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto& p = proc.getPatch();
        for (size_t k = 0; k < rows.size(); ++k)
            rows[k]->slider.setValue (params[k].get (p), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::ground());

        auto header = getLocalBounds().removeFromTop (headerH);
        g.setColour (Theme::ink());
        g.setFont (Theme::mono (11.0f, true));
        g.drawText ("DEV / 777", header.reduced (2, 0), juce::Justification::centredLeft, false);
        g.fillRect (0, headerH - 1, getWidth(), 1);
        g.drawRect (getLocalBounds(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (1);
        auto header = r.removeFromTop (headerH - 1);
        resetButton.setBounds (header.removeFromRight (64).reduced (2));

        viewport.setBounds (r);
        content.setSize (juce::jmax (260, r.getWidth() - 10), (int) rows.size() * rowHeight);

        int y = 0;
        for (auto& row : rows)
        {
            auto line = juce::Rectangle<int> (0, y, content.getWidth(), rowHeight).reduced (4, 1);
            row->label.setBounds (line.removeFromLeft (112));
            row->slider.setBounds (line);
            y += rowHeight;
        }
    }

private:
    struct Row
    {
        juce::Label  label;
        juce::Slider slider;
    };

    struct ParamDesc
    {
        juce::String name;
        float min, max, step;
        double skewMid;                                  // 0 = linear
        std::function<float (const Patch&)> get;
        std::function<void (Patch&, float)> set;
    };

    void add (juce::String name, float min, float max, float step,
              std::function<float (const Patch&)> get,
              std::function<void (Patch&, float)> set,
              double skewMid = 0.0)
    {
        params.push_back ({ std::move (name), min, max, step, skewMid,
                            std::move (get), std::move (set) });
    }

    void buildParams()
    {
        // Oscillators
        for (int k = 0; k < 3; ++k)
        {
            const juce::String tag = "osc" + juce::String (k + 1) + " ";
            add (tag + "wave",  0.0f, 5.0f, 1.0f,   // 4 noise, 5 wavetable
                 [k] (const Patch& p) { return (float) p.osc[k].wave; },
                 [k] (Patch& p, float v) { p.osc[k].wave = (int) v; });
            add (tag + "semi", -24.0f, 24.0f, 1.0f,
                 [k] (const Patch& p) { return (float) p.osc[k].semi; },
                 [k] (Patch& p, float v) { p.osc[k].semi = (int) v; });
            add (tag + "fine", -50.0f, 50.0f, 0.1f,
                 [k] (const Patch& p) { return p.osc[k].fine; },
                 [k] (Patch& p, float v) { p.osc[k].fine = v; });
            add (tag + "level", 0.0f, 1.0f, 0.01f,
                 [k] (const Patch& p) { return p.osc[k].level; },
                 [k] (Patch& p, float v) { p.osc[k].level = v; });
            add (tag + "decay", 0.0f, 1.0f, 0.005f,     // 0 = follow the amp env
                 [k] (const Patch& p) { return p.osc[k].decay; },
                 [k] (Patch& p, float v) { p.osc[k].decay = v; });
        }

        addI ("osc mode",    0.0f, 3.0f, 1.0f,  &Patch::oscMode);
        addM ("fm amount",   0.0f, 1.0f, 0.01f, &Patch::fmAmount);

        // Filter
        addI ("filter model", 0.0f, 4.0f, 1.0f, &Patch::filterModel);
        addI ("filter poles", 2.0f, 4.0f, 2.0f, &Patch::filterPoles);
        addI ("filter type",  0.0f, 2.0f, 1.0f, &Patch::filterType);
        addM ("cutoff",      20.0f, 12000.0f, 1.0f, &Patch::cutoff, 1000.0);
        addM ("resonance",    0.0f, 1.0f, 0.01f, &Patch::resonance);
        addM ("filter env",  -1.0f, 1.0f, 0.01f, &Patch::filterEnvAmt);
        addM ("keytrack",     0.0f, 1.0f, 0.01f, &Patch::keytrack);
        addM ("morph/vowel",  0.0f, 1.0f, 0.01f, &Patch::filterMorph);

        // Envelopes
        addM ("amp A", 0.001f, 3.0f, 0.001f, &Patch::ampA, 0.3);
        addM ("amp D", 0.005f, 3.0f, 0.001f, &Patch::ampD, 0.3);
        addM ("amp S", 0.0f,   1.0f, 0.01f,  &Patch::ampS);
        addM ("amp R", 0.005f, 4.0f, 0.001f, &Patch::ampR, 0.4);
        addM ("mod A", 0.001f, 3.0f, 0.001f, &Patch::modA, 0.3);
        addM ("mod D", 0.005f, 3.0f, 0.001f, &Patch::modD, 0.3);
        addM ("mod S", 0.0f,   1.0f, 0.01f,  &Patch::modS);
        addM ("mod R", 0.005f, 4.0f, 0.001f, &Patch::modR, 0.4);

        // Modulators. dest: 0 off, 1 pitch, 2 cutoff, 3 amp, 4 pulse width,
        // 5 fm, 6 pan, 7 detune, 8 resonance, 9 osc2, 10 sub, 11 noise.
        // shape: 0 sine, 1 tri, 2 square, 3 ramp up, 4 ramp down, 5 s&h, 6 walk.
        for (int k = 0; k < Patch::NumMods; ++k)
        {
            const juce::String tag = "mod" + juce::String (k + 1) + " ";
            add (tag + "dest", 0.0f, (float) (NumModDests - 1), 1.0f,
                 [k] (const Patch& p) { return (float) p.mod[k].dest; },
                 [k] (Patch& p, float v) { p.mod[k].dest = (int) std::round (v); });
            add (tag + "shape", 0.0f, (float) (NumModShapes - 1), 1.0f,
                 [k] (const Patch& p) { return (float) p.mod[k].shape; },
                 [k] (Patch& p, float v) { p.mod[k].shape = (int) std::round (v); });
            add (tag + "rate", 0.02f, 20.0f, 0.01f,
                 [k] (const Patch& p) { return p.mod[k].rate; },
                 [k] (Patch& p, float v) { p.mod[k].rate = v; }, 2.0);
            add (tag + "depth", 0.0f, 1.0f, 0.01f,
                 [k] (const Patch& p) { return p.mod[k].depth; },
                 [k] (Patch& p, float v) { p.mod[k].depth = v; });
            add (tag + "sync", 0.0f, 7.0f, 1.0f,
                 [k] (const Patch& p) { return (float) p.mod[k].syncDiv; },
                 [k] (Patch& p, float v) { p.mod[k].syncDiv = (int) std::round (v); });
        }

        // Wavetable. Set an oscillator's wave to 5 to use it.
        // table: 0 morph, 1 sweep, 2 vowel, 3 hollow, 4 glass, 5 grit.
        addI ("wt table", 0.0f, (float) (Wavetables::NumTables - 1), 1.0f, &Patch::wtTable);
        addM ("wt pos",   0.0f, 1.0f, 0.01f, &Patch::wtPos);

        // Arp. mode: 0 off, 1 up, 2 down, 3 up-down, 4 random.
        addI ("arp mode",    0.0f, 4.0f, 1.0f, &Patch::arpMode);
        addI ("arp div",     1.0f, 7.0f, 1.0f, &Patch::arpDiv);
        addM ("arp gate",    0.05f, 0.98f, 0.01f, &Patch::arpGate);
        addI ("arp octaves", 1.0f, 3.0f, 1.0f, &Patch::arpOctaves);

        addI ("env dest",   0.0f, (float) (NumModDests - 1), 1.0f, &Patch::envDest);
        addM ("env amount", -1.0f, 1.0f, 0.01f, &Patch::envAmount);

        // Layers
        addI ("unison",       1.0f, 7.0f,  1.0f,  &Patch::unisonVoices);
        addM ("uni detune",   0.0f, 50.0f, 0.1f,  &Patch::unisonDetune);
        addI ("sub wave",     0.0f, 3.0f,  3.0f,  &Patch::subWave);
        addM ("sub level",    0.0f, 1.0f,  0.01f, &Patch::subLevel);
        addM ("noise",        0.0f, 1.0f,  0.01f, &Patch::noiseLevel);
        addM ("pulse width",  0.05f, 0.95f, 0.01f, &Patch::pulseWidth);
        addM ("pwm depth",    0.0f, 1.0f,  0.01f, &Patch::pwmDepth);
        addM ("vel>filter",   0.0f, 1.0f,  0.01f, &Patch::velToFilter);
        addM ("vel>amp",      0.0f, 1.0f,  0.01f, &Patch::velToAmp);
        addI ("voice mode",   0.0f, 2.0f,  1.0f,  &Patch::voiceMode);
        addM ("glide",        0.0f, 0.5f,  0.001f, &Patch::glideTime);
        addM ("stereo width", 0.0f, 1.0f,  0.01f, &Patch::stereoWidth);

        // FX — character
        addM ("drive",       0.0f, 1.0f,  0.01f, &Patch::drive);
        addM ("fold",        0.0f, 1.0f,  0.01f, &Patch::foldAmount);
        addM ("crush bits",  2.0f, 16.0f, 0.1f,  &Patch::crushBits);
        addM ("crush rate",  1.0f, 32.0f, 0.1f,  &Patch::crushRate);

        // FX — modulation
        addM ("phaser mix",   0.0f, 1.0f, 0.01f, &Patch::phaserMix);
        addM ("phaser rate",  0.02f, 5.0f, 0.01f, &Patch::phaserRate, 0.6);
        addM ("phaser depth", 0.0f, 1.0f, 0.01f, &Patch::phaserDepth);
        addM ("phaser fb",    0.0f, 0.85f, 0.01f, &Patch::phaserFb);
        addM ("flanger mix",  0.0f, 1.0f, 0.01f, &Patch::flangerMix);
        addM ("flanger rate", 0.02f, 4.0f, 0.01f, &Patch::flangerRate, 0.5);
        addM ("flanger depth",0.0f, 1.0f, 0.01f, &Patch::flangerDepth);
        addM ("flanger fb",   0.0f, 0.9f, 0.01f, &Patch::flangerFb);
        addM ("chorus",       0.0f, 1.0f, 0.01f, &Patch::chorusMix);

        // FX — time & dynamics
        addM ("delay time", 0.02f, 1.0f, 0.001f, &Patch::delayTime);
        addM ("delay fb",   0.0f,  0.95f, 0.01f, &Patch::delayFb);
        addM ("delay mix",  0.0f,  1.0f, 0.01f,  &Patch::delayMix);
        addI ("delay sync", 0.0f,  7.0f, 1.0f,   &Patch::delaySyncDiv);
        addM ("reverb size",0.0f,  1.0f, 0.01f,  &Patch::reverbSize);
        addM ("reverb mix", 0.0f,  1.0f, 0.01f,  &Patch::reverbMix);
        addM ("compressor", 0.0f,  1.0f, 0.01f,  &Patch::compAmount);
        addM ("master",     0.0f,  1.0f, 0.01f,  &Patch::master);
    }

    // Float member helper.
    void addM (juce::String name, float min, float max, float step,
               float Patch::* member, double skewMid = 0.0)
    {
        add (std::move (name), min, max, step,
             [member] (const Patch& p) { return p.*member; },
             [member] (Patch& p, float v) { p.*member = v; },
             skewMid);
    }

    // Int member helper.
    void addI (juce::String name, float min, float max, float step, int Patch::* member)
    {
        add (std::move (name), min, max, step,
             [member] (const Patch& p) { return (float) (p.*member); },
             [member] (Patch& p, float v) { p.*member = (int) std::round (v); });
    }

    static constexpr int rowHeight = 20;
    static constexpr int headerH   = 20;

    GambleSynthProcessor& proc;
    juce::Viewport  viewport;
    juce::Component content;
    juce::TextButton resetButton;
    std::vector<ParamDesc> params;
    std::vector<std::unique_ptr<Row>> rows;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevPanel)
};
