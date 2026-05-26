/*
 * Surge XT - a free and open source hybrid synthesizer,
 * built by Surge Synth Team
 *
 * Learn more at https://surge-synthesizer.github.io/
 *
 * Copyright 2018-2024, various authors, as described in the GitHub
 * transaction log.
 *
 * Surge XT is released under the GNU General Public Licence v3
 * or later (GPL-3.0-or-later). The license is found in the "LICENSE"
 * file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 */

#ifndef SURGE_SRC_SURGE_SINGLE_FX_SINGLEFXPROCESSOR_H
#define SURGE_SRC_SURGE_SINGLE_FX_SINGLEFXPROCESSOR_H

// Header ordering matters for ARM
#include <juce_gui_extra/juce_gui_extra.h>

#include "SurgeStorage.h"
#include "Effect.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <string>

#ifndef SURGE_SINGLE_FX_TYPE
#define SURGE_SINGLE_FX_TYPE fxt_chorus4
#endif

#ifndef SURGE_SINGLE_FX_NAME
#define SURGE_SINGLE_FX_NAME "Chorus"
#endif

class SingleFXProcessor : public juce::AudioProcessor
{
  public:
    SingleFXProcessor();
    ~SingleFXProcessor() override;

    float input_buffer alignas(16)[2][BLOCK_SIZE]{};
    float output_buffer alignas(16)[2][BLOCK_SIZE]{};
    int input_position{0};
    int output_position{-1};
    bool nonLatentBlockMode{true};

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void reset() override;

    juce::AudioProcessorEditor *createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return SURGE_SINGLE_FX_NAME; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // Describes how a JUCE parameter's display value maps to Surge's internal float.
    struct ParamDisplayRange
    {
        float min{0.f}, max{1.f}, def{0.f};
        float surge_min{0.f}, surge_max{1.f};
        float a{1.f}, b{1.f}, scale{1.f};
        enum class Conv
        {
            Raw,       // keep 0..1 normalized (integer/bool/custom params)
            Linear,    // display = surge_val * scale
            ATwoBx,    // display = a * 2^(surge_val * b)  [Hz]
            ATwoBxMs   // display = a * 2^(surge_val * b) * 1000  [ms]
        } conv{Conv::Raw};
        std::string label;
    };

  private:
    std::unique_ptr<SurgeStorage> storage;
    FxStorage *fxstorage{nullptr};
    std::unique_ptr<Effect> surge_effect;

    int fx_param_remap[n_fx_params]{};
    int storage_id_start{0}, storage_id_end{0};
    double lastBPM{-1.0};

    juce::AudioParameterFloat *fxParams[n_fx_params]{};
    ParamDisplayRange param_range[n_fx_params];

    bool getParamEnabled(int i) const
    {
        return fxstorage->p[fx_param_remap[i]].ctrltype != ct_none;
    }
    std::string getParamName(int i) const
    {
        return fxstorage->p[fx_param_remap[i]].get_name();
    }

    static ParamDisplayRange computeRange(const Parameter &p);
    float juceToF01(int paramIdx, float juceVal) const;
    float f01ToJuce(int paramIdx, float f01) const;

    void reorderSurgeParams();
    void setupStorageRanges(Parameter *start, Parameter *endIncluding);
    void copyGlobaldataSubset(int start, int end);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SingleFXProcessor)
};

#endif
