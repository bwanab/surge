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

#include "SingleFXProcessor.h"
#include "UserDefaults.h"
#include <fmt/core.h>
#include <cmath>

#if LINUX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

//==============================================================================
SingleFXProcessor::ParamDisplayRange SingleFXProcessor::computeRange(const Parameter &p)
{
    ParamDisplayRange r;
    r.surge_min = p.val_min.f;
    r.surge_max = p.val_max.f;

    // Integer, bool, or custom display: keep 0..1 normalized passthrough
    if (p.valtype != vt_float || p.displayType == Parameter::Custom ||
        p.displayType == Parameter::DelegatedToFormatter)
    {
        r.conv = ParamDisplayRange::Conv::Raw;
        r.min = 0.f;
        r.max = 1.f;
        r.def = p.get_value_f01();
        return r;
    }

    r.label = p.displayInfo.unit;

    if (p.displayType == Parameter::LinearWithScale)
    {
        r.conv = ParamDisplayRange::Conv::Linear;
        r.scale = (p.displayInfo.scale > 0.f) ? p.displayInfo.scale : 1.f;
        r.min = p.val_min.f * r.scale;
        r.max = p.val_max.f * r.scale;
        r.def = p.val.f * r.scale;
    }
    else if (p.displayType == Parameter::ATwoToTheBx)
    {
        r.a = (p.displayInfo.a > 0.f) ? p.displayInfo.a : 1.f;
        r.b = (p.displayInfo.b != 0.f) ? p.displayInfo.b : 1.f;

        float minDisp = r.a * std::pow(2.f, p.val_min.f * r.b);
        float maxDisp = r.a * std::pow(2.f, p.val_max.f * r.b);
        float defDisp = r.a * std::pow(2.f, p.val.f * r.b);

        bool isMs = (p.displayInfo.customFeatures & Parameter::kSwitchesFromSecToMillisec) != 0;
        if (isMs)
        {
            r.conv = ParamDisplayRange::Conv::ATwoBxMs;
            r.label = "ms";
            r.min = minDisp * 1000.f;
            r.max = maxDisp * 1000.f;
            r.def = defDisp * 1000.f;
        }
        else
        {
            r.conv = ParamDisplayRange::Conv::ATwoBx;
            r.min = minDisp;
            r.max = maxDisp;
            r.def = defDisp;
        }
    }
    else if (p.displayType == Parameter::Decibel)
    {
        // Decibel params store values directly in dB
        r.conv = ParamDisplayRange::Conv::Linear;
        r.scale = 1.f;
        r.label = "dB";
        r.min = p.val_min.f;
        r.max = p.val_max.f;
        r.def = p.val.f;
    }
    else
    {
        r.conv = ParamDisplayRange::Conv::Raw;
        r.min = 0.f;
        r.max = 1.f;
        r.def = p.get_value_f01();
    }
    return r;
}

// Convert a JUCE parameter display value back to Surge's f01 [0..1]
float SingleFXProcessor::juceToF01(int paramIdx, float juceVal) const
{
    const auto &r = param_range[paramIdx];
    float internal;
    switch (r.conv)
    {
    case ParamDisplayRange::Conv::Raw:
        return juceVal;
    case ParamDisplayRange::Conv::Linear:
        internal = (r.scale != 0.f) ? juceVal / r.scale : juceVal;
        break;
    case ParamDisplayRange::Conv::ATwoBx:
        internal = (r.b != 0.f) ? std::log2f(std::max(juceVal, 1e-10f) / r.a) / r.b : 0.f;
        break;
    case ParamDisplayRange::Conv::ATwoBxMs:
        internal =
            (r.b != 0.f) ? std::log2f(std::max(juceVal, 1e-10f) / 1000.f / r.a) / r.b : 0.f;
        break;
    }
    float range = r.surge_max - r.surge_min;
    return (range > 0.f) ? (internal - r.surge_min) / range : 0.f;
}

// Convert Surge's f01 to a JUCE display value
float SingleFXProcessor::f01ToJuce(int paramIdx, float f01) const
{
    const auto &r = param_range[paramIdx];
    switch (r.conv)
    {
    case ParamDisplayRange::Conv::Raw:
        return f01;
    case ParamDisplayRange::Conv::Linear:
    {
        float internal = r.surge_min + f01 * (r.surge_max - r.surge_min);
        return internal * r.scale;
    }
    case ParamDisplayRange::Conv::ATwoBx:
    {
        float internal = r.surge_min + f01 * (r.surge_max - r.surge_min);
        return r.a * std::pow(2.f, internal * r.b);
    }
    case ParamDisplayRange::Conv::ATwoBxMs:
    {
        float internal = r.surge_min + f01 * (r.surge_max - r.surge_min);
        return r.a * std::pow(2.f, internal * r.b) * 1000.f;
    }
    }
    return f01;
}

//==============================================================================
SingleFXProcessor::SingleFXProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    auto cfg = SurgeStorage::SurgeStorageConfig::fromDataPath("");
    cfg.createUserDirectory = false;
    cfg.scanWavetableAndPatches = false;
    storage.reset(new SurgeStorage(cfg));

    nonLatentBlockMode = !juce::PluginHostType().isFruityLoops();
    nonLatentBlockMode = Surge::Storage::getUserDefaultValue(
        storage.get(), Surge::Storage::FXUnitAssumeFixedBlock, nonLatentBlockMode);
    setLatencySamples(nonLatentBlockMode ? 0 : BLOCK_SIZE);

    fxstorage = &(storage->getPatch().fx[0]);
    fxstorage->type.val.i = SURGE_SINGLE_FX_TYPE;

    surge_effect.reset(spawn_effect(SURGE_SINGLE_FX_TYPE, storage.get(),
                                    &(storage->getPatch().fx[0]),
                                    storage->getPatch().globaldata));
    surge_effect->init();
    surge_effect->init_ctrltypes();
    surge_effect->init_default_values();

    reorderSurgeParams();
    setupStorageRanges(&(fxstorage->type), &(fxstorage->p[n_fx_params - 1]));

    for (int i = 0; i < n_fx_params; ++i)
    {
        std::string lb = fmt::format("fx_parm_{:d}", i);
        std::string nm;
        if (getParamEnabled(i))
            nm = getParamName(i);
        else
            nm = fmt::format("_unused_{:d}", i);

        param_range[i] = computeRange(fxstorage->p[fx_param_remap[i]]);
        const auto &pr = param_range[i];

        auto attrs = juce::AudioParameterFloatAttributes().withLabel(pr.label);

        addParameter(fxParams[i] = new juce::AudioParameterFloat(
                         juce::ParameterID(lb, 1), nm,
                         juce::NormalisableRange<float>(pr.min, pr.max), pr.def, attrs));
    }
}

SingleFXProcessor::~SingleFXProcessor() {}

//==============================================================================
void SingleFXProcessor::prepareToPlay(double sr, int /*samplesPerBlock*/)
{
    storage->setSamplerate(sr);
    storage->songpos = 0.0;
    setLatencySamples(nonLatentBlockMode ? 0 : BLOCK_SIZE);
}

void SingleFXProcessor::releaseResources() {}

void SingleFXProcessor::reset()
{
    if (surge_effect)
        surge_effect->init();
}

bool SingleFXProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    bool inputValid = layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono() ||
                      layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    bool outputValid = layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
                       layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    return inputValid && outputValid;
}

#define is_aligned(POINTER, BYTE_COUNT) (((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

void SingleFXProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                     juce::MidiBuffer & /*midiMessages*/)
{
    if (!surge_effect)
        return;

    juce::ScopedNoDenormals noDenormals;

    float thisBPM = 120.0f;
    if (auto *playhead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo cp;
        playhead->getCurrentPosition(cp);
        thisBPM = (float)cp.bpm;
    }
    if (storage && thisBPM != lastBPM)
    {
        lastBPM = thisBPM;
        storage->temposyncratio = thisBPM / 120.0;
        storage->temposyncratio_inv = 1.0 / storage->temposyncratio;
    }

    if (surge_effect->checkHasInvalidatedUI())
    {
        for (int i = 0; i < n_fx_params; ++i)
            *(fxParams[i]) = f01ToJuce(i, fxstorage->p[fx_param_remap[i]].get_value_f01());
    }

    auto sampl = buffer.getNumSamples();
    if (nonLatentBlockMode && ((sampl & ~(BLOCK_SIZE - 1)) != sampl))
    {
        nonLatentBlockMode = false;
        input_position = 0;
        output_position = 0;
        memset(output_buffer, 0, sizeof(output_buffer));
        memset(input_buffer, 0, sizeof(input_buffer));
        setLatencySamples(BLOCK_SIZE);
        updateHostDisplay(ChangeDetails().withLatencyChanged(true));
    }

    auto mainInput = getBusBuffer(buffer, true, 0);
    auto mainOutput = getBusBuffer(buffer, false, 0);

    int inChanL = 0, inChanR = 1;
    if (mainInput.getNumChannels() == 1)
        inChanR = 0;

    if (nonLatentBlockMode)
    {
        for (int outPos = 0; outPos < buffer.getNumSamples(); outPos += BLOCK_SIZE)
        {
            auto outL = mainOutput.getWritePointer(0, outPos);
            auto outR = mainOutput.getWritePointer(1, outPos);

            for (int i = 0; i < n_fx_params; ++i)
                fxstorage->p[fx_param_remap[i]].set_value_f01(juceToF01(i, *fxParams[i]));
            copyGlobaldataSubset(storage_id_start, storage_id_end);

            auto inL = mainInput.getReadPointer(inChanL, outPos);
            auto inR = mainInput.getReadPointer(inChanR, outPos);

            if (is_aligned(outL, 16) && is_aligned(outR, 16) && inL == outL && inR == outR)
            {
                surge_effect->process_ringout(outL, outR, true);
            }
            else
            {
                float bufL alignas(16)[BLOCK_SIZE], bufR alignas(16)[BLOCK_SIZE];
                memcpy(bufL, inL, BLOCK_SIZE * sizeof(float));
                memcpy(bufR, inR, BLOCK_SIZE * sizeof(float));
                surge_effect->process_ringout(bufL, bufR, true);
                memcpy(outL, bufL, BLOCK_SIZE * sizeof(float));
                memcpy(outR, bufR, BLOCK_SIZE * sizeof(float));
            }
        }
    }
    else
    {
        auto outL = mainOutput.getWritePointer(0, 0);
        auto outR = mainOutput.getWritePointer(1, 0);
        auto inL = mainInput.getReadPointer(inChanL, 0);
        auto inR = mainInput.getReadPointer(inChanR, 0);

        for (int smp = 0; smp < buffer.getNumSamples(); smp++)
        {
            input_buffer[0][input_position] = inL[smp];
            input_buffer[1][input_position] = inR[smp];
            input_position++;

            if (input_position == BLOCK_SIZE)
            {
                for (int i = 0; i < n_fx_params; ++i)
                    fxstorage->p[fx_param_remap[i]].set_value_f01(juceToF01(i, *fxParams[i]));
                copyGlobaldataSubset(storage_id_start, storage_id_end);
                surge_effect->process_ringout(input_buffer[0], input_buffer[1], true);
                memcpy(output_buffer, input_buffer, 2 * BLOCK_SIZE * sizeof(float));
                input_position = 0;
                output_position = 0;
            }

            if (output_position >= 0 && output_position < BLOCK_SIZE)
            {
                outL[smp] = output_buffer[0][output_position];
                outR[smp] = output_buffer[1][output_position];
                output_position++;
            }
            else
            {
                outL[smp] = outR[smp] = 0.0f;
            }
        }
    }
}

//==============================================================================
void SingleFXProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto xml = std::make_unique<juce::XmlElement>("surgefx-single");
    for (int i = 0; i < n_fx_params; ++i)
        xml->setAttribute(fmt::format("p{:d}", i).c_str(),
                          (double)juceToF01(i, *fxParams[i]));
    xml->setAttribute("fxt", SURGE_SINGLE_FX_TYPE);
    copyXmlToBinary(*xml, destData);
}

void SingleFXProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml && xml->hasTagName("surgefx-single"))
    {
        for (int i = 0; i < n_fx_params; ++i)
        {
            auto key = fmt::format("p{:d}", i);
            if (xml->hasAttribute(key.c_str()))
            {
                float f01 = (float)xml->getDoubleAttribute(key.c_str(), 0.5);
                *(fxParams[i]) = f01ToJuce(i, f01);
                fxstorage->p[fx_param_remap[i]].set_value_f01(f01);
            }
        }
    }
}

//==============================================================================
void SingleFXProcessor::reorderSurgeParams()
{
    for (int i = 0; i < n_fx_params; ++i)
        fx_param_remap[i] = i;

    if (!surge_effect)
        return;

    std::vector<std::pair<int, int>> orderTrack;
    for (int i = 0; i < n_fx_params; ++i)
    {
        if (fxstorage->p[i].posy_offset && fxstorage->p[i].ctrltype != ct_none)
            orderTrack.push_back({i, i * 2 + fxstorage->p[i].posy_offset});
        else
            orderTrack.push_back({i, 10000});
    }
    std::sort(orderTrack.begin(), orderTrack.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    for (int i = 0; i < n_fx_params; ++i)
        fx_param_remap[i] = orderTrack[i].first;
}

void SingleFXProcessor::setupStorageRanges(Parameter *start, Parameter *endIncluding)
{
    int min_id = 100000, max_id = -1;
    for (Parameter *p = start; p <= endIncluding; ++p)
    {
        if (p->id >= 0)
        {
            min_id = std::min(min_id, p->id);
            max_id = std::max(max_id, p->id);
        }
    }
    storage_id_start = min_id;
    storage_id_end = max_id + 1;
}

void SingleFXProcessor::copyGlobaldataSubset(int start, int end)
{
    for (int i = start; i < end; ++i)
        storage->getPatch().globaldata[i].i = storage->getPatch().param_ptr[i]->val.i;
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new SingleFXProcessor(); }

#if LINUX
#pragma GCC diagnostic pop
#endif
