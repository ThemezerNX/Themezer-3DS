// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/sfx.hpp"

#include "core/bcstm.hpp"
#include "sys/audio.hpp"
#include "sys/log.hpp"
#include "sys/thread.hpp"

#include <3ds.h>
#include <array>
#include <cstdio>
#include <optional>

namespace tz::sfx
{

namespace
{

constexpr std::array<const char*, static_cast<size_t>(Sound::Count)> kNames{
    "cursor", "page", "tab", "open", "close", "confirm", "back", "done", "error",
};
// their levels are in the clips
constexpr float kVolume = 1.0f;

// A mono DSP-ADPCM BCSTM, read whole
struct Clip
{
    uint8_t* file = nullptr; // in linear memory, for the DSP
    const uint8_t* samples = nullptr;
    u32 count = 0;
    float rate = 0;
    std::array<u16, 16> coefficients{};
    ndspAdpcmData start{};
};

std::array<Clip, static_cast<size_t>(Sound::Count)> clips;
std::array<ndspWaveBuf, 2> buffers{};
std::array<ndspAdpcmData, 2> contexts{}; // the playing clip's decoder state, which the DSP carries on from
bool loaded = false;
bool enabled = true;

int channelOf(Sound sound)
{
    return audio::kEffectChannel + (sound <= Sound::Tab ? 0 : 1);
}

Clip load(const char* name)
{
    char path[64];
    std::snprintf(path, sizeof(path), "romfs:/sfx/%s.bcstm", name);
    FILE* file = std::fopen(path, "rb");
    if (!file)
    {
        LOG_WARN("sfx: %s is missing", path);
        return {};
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    Clip clip;
    clip.file = size > 0 ? static_cast<uint8_t*>(linearAlloc(static_cast<size_t>(size))) : nullptr;
    const bool read =
        clip.file && std::fread(clip.file, 1, static_cast<size_t>(size), file) == static_cast<size_t>(size);
    std::fclose(file);
    std::optional<bcstm::Info> info;
    if (read)
        if (auto parsed = bcstm::parse(ByteSpan(clip.file, static_cast<size_t>(size))))
            info = *parsed;
    // one channel's blocks follow each other, so its samples are all in a row: 8 bytes for 14 samples
    if (!info || info->encoding != bcstm::Encoding::DspAdpcm || info->channels != 1 ||
        info->dataOffset + (uint64_t(info->loopEnd) + 13) / 14 * 8 > static_cast<uint64_t>(size))
    {
        LOG_WARN("sfx: %s isn't a mono DSP-ADPCM BCSTM", path);
        if (clip.file)
            linearFree(clip.file);
        return {};
    }
    clip.samples = clip.file + info->dataOffset;
    clip.count = info->loopEnd;
    clip.rate = static_cast<float>(info->sampleRate);
    const bcstm::AdpcmChannel& adpcm = info->adpcm[0];
    for (size_t i = 0; i < clip.coefficients.size(); ++i)
        clip.coefficients[i] = static_cast<u16>(adpcm.coefficients[i]);
    clip.start = ndspAdpcmData{adpcm.predictorScale, adpcm.history1, adpcm.history2};
    DSP_FlushDataCache(clip.file, static_cast<u32>(size));
    return clip;
}

} // namespace

void init()
{
    if (!audio::available())
        return;
    for (size_t i = 0; i < clips.size(); ++i)
        clips[i] = load(kNames[i]);
    for (int channel = audio::kEffectChannel; channel < audio::kEffectChannel + 2; ++channel)
    {
        ndspChnReset(channel);
        ndspChnSetFormat(channel, NDSP_FORMAT_MONO_ADPCM);
        ndspChnSetInterp(channel, NDSP_INTERP_POLYPHASE); // the clips are made at a lower rate than the DSP's
        float mix[12]{};
        mix[0] = mix[1] = kVolume;
        ndspChnSetMix(channel, mix);
    }
    loaded = true;
}

void exit()
{
    if (!loaded)
        return;
    loaded = false;
    for (int channel = audio::kEffectChannel; channel < audio::kEffectChannel + 2; ++channel)
        ndspChnReset(channel);
    // the DSP may read the clips until its next frame
    sleepMs(10);
    for (Clip& clip : clips)
    {
        if (clip.file)
            linearFree(clip.file);
        clip = {};
    }
}

void setEnabled(bool on)
{
    enabled = on;
}

void play(Sound sound)
{
    Clip& clip = clips[static_cast<size_t>(sound)];
    if (!loaded || !enabled || clip.count == 0)
        return;
    const int channel = channelOf(sound);
    ndspChnWaveBufClear(channel);
    const size_t slot = static_cast<size_t>(channel - audio::kEffectChannel);
    ndspChnSetRate(channel, clip.rate);
    ndspChnSetAdpcmCoefs(channel, clip.coefficients.data());
    contexts[slot] = clip.start;
    ndspWaveBuf& buffer = buffers[slot];
    buffer = ndspWaveBuf{};
    buffer.data_vaddr = clip.samples;
    buffer.nsamples = clip.count;
    buffer.adpcm_data = &contexts[slot];
    ndspChnWaveBufAdd(channel, &buffer);
}

} // namespace tz::sfx
