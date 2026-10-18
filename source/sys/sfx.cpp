// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "sys/sfx.hpp"

#include "sys/audio.hpp"
#include "sys/log.hpp"
#include "sys/thread.hpp"

#include <3ds.h>
#include <array>
#include <cstdio>

namespace tz::sfx
{

namespace
{

constexpr std::array<const char*, static_cast<size_t>(Sound::Count)> kNames{
    "cursor", "page", "tab", "open", "close", "confirm", "back", "done", "error",
};
// their levels are in the clips
constexpr float kVolume = 1.0f;

struct Clip
{
    int16_t* samples = nullptr; // in linear memory, for the DSP
    u32 count = 0;
};

std::array<Clip, static_cast<size_t>(Sound::Count)> clips;
std::array<ndspWaveBuf, 2> buffers{};
bool loaded = false;
bool enabled = true;

int channelOf(Sound sound)
{
    return audio::kEffectChannel + (sound <= Sound::Tab ? 0 : 1);
}

Clip load(const char* name)
{
    char path[64];
    std::snprintf(path, sizeof(path), "romfs:/sfx/%s.pcm", name);
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
    clip.samples = size > 0 ? static_cast<int16_t*>(linearAlloc(static_cast<size_t>(size))) : nullptr;
    if (clip.samples && std::fread(clip.samples, 1, static_cast<size_t>(size), file) == static_cast<size_t>(size))
    {
        clip.count = static_cast<u32>(size) / sizeof(int16_t);
        DSP_FlushDataCache(clip.samples, static_cast<u32>(size));
    }
    std::fclose(file);
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
        ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(channel, 32728.0f); // the DSP's own rate, which the clips are made at
        ndspChnSetInterp(channel, NDSP_INTERP_NONE);
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
        if (clip.samples)
            linearFree(clip.samples);
        clip = {};
    }
}

void setEnabled(bool on)
{
    enabled = on;
}

void play(Sound sound)
{
    const Clip& clip = clips[static_cast<size_t>(sound)];
    if (!loaded || !enabled || clip.count == 0)
        return;
    const int channel = channelOf(sound);
    ndspChnWaveBufClear(channel);
    ndspWaveBuf& buffer = buffers[static_cast<size_t>(channel - audio::kEffectChannel)];
    buffer = ndspWaveBuf{};
    buffer.data_vaddr = clip.samples;
    buffer.nsamples = clip.count;
    ndspChnWaveBufAdd(channel, &buffer);
}

} // namespace tz::sfx
