// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#ifdef TZ_DEVTOOLS

#include "app/autopilot.hpp"

#include "app/app.hpp"
#include "core/image.hpp"
#include "gfx/draw.hpp"
#include "gfx/renderer.hpp"
#include "gfx/text.hpp"
#include "sys/log.hpp"
#include "ui/keyboard.hpp"

#include <3ds.h>
#include <cctype>
#include <charconv>
#include <malloc.h>
#include <optional>
#include <string>
#include <vector>

namespace tz::autopilot
{

namespace
{

constexpr const char* kScriptPath = "/3ds/Themezer/autopilot.txt";
constexpr const char* kOutputDir = "/3ds/Themezer/autopilot";

struct Step
{
    enum class Kind : uint8_t
    {
        Wait,
        Hold,
        Touch,
        Drag,
        Shot,
        Log,
        Glyphs,
        DumpFont,
        SaveLog,
        Quit,
        Monkey,
        Memory,
        Type,
    };
    Kind kind = Kind::Wait;
    int frames = 0; // 0 for the steps that take no time
    u32 keys = 0;
    float x = 0;
    float y = 0;
    float toX = 0;
    float toY = 0;
    std::string text;
};

struct KeyName
{
    std::string_view name;
    u32 key;
};

constexpr KeyName kKeys[] = {
    {"A", KEY_A},       {"B", KEY_B},         {"X", KEY_X},       {"Y", KEY_Y},        {"L", KEY_L},
    {"R", KEY_R},       {"ZL", KEY_ZL},       {"ZR", KEY_ZR},     {"START", KEY_START}, {"SELECT", KEY_SELECT},
    {"UP", KEY_DUP},    {"DOWN", KEY_DDOWN},  {"LEFT", KEY_DLEFT}, {"RIGHT", KEY_DRIGHT},
};

constexpr u32 kMonkeyKeys = KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_START | KEY_SELECT | KEY_DUP |
                            KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;

std::vector<Step> steps;
size_t current = 0;
int frame = 0;
bool running = false;
bool finished = false; // the script has ended; the done file is still to write
bool quit = false;
std::vector<std::string> shots;
// repeat blocks being read: how many times, and where their steps start
std::vector<std::pair<int, size_t>> repeats;
// what the keyboard answers next (type), nothing for cancel
std::vector<std::optional<std::string>> typed;
bool monkeying = false;

// The monkey: what it does now, and for how many more frames.
struct Monkey
{
    u32 random = 1;
    u32 keys = 0;
    bool touching = false;
    float x = 0, y = 0, toX = 0, toY = 0;
    int length = 0; // frames of the action
    int left = 0;   // frames of it still to go
    int gap = 0;    // frames of nothing before the next

    u32 next()
    {
        // xorshift32
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    }
    int between(int low, int high) { return low + static_cast<int>(next() % static_cast<u32>(high - low + 1)); }
} monkey;

std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        const size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

template <class T>
std::optional<T> number(std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<u32> keys(std::string_view text)
{
    u32 result = 0;
    while (!text.empty())
    {
        const size_t plus = text.find('+');
        std::string name(text.substr(0, plus));
        for (char& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        bool known = false;
        for (const KeyName& key : kKeys)
        {
            if (key.name == name)
            {
                result |= key.key;
                known = true;
            }
        }
        if (!known)
            return std::nullopt;
        text = plus == std::string_view::npos ? std::string_view() : text.substr(plus + 1);
    }
    return result == 0 ? std::nullopt : std::optional<u32>(result);
}

// One line of the script as steps; false when it isn't a command.
bool parse(std::string_view line, std::vector<Step>& out)
{
    if (const size_t hash = line.find('#'); hash != std::string_view::npos)
        line = line.substr(0, hash);
    const std::vector<std::string_view> w = words(line);
    if (w.empty())
        return true;
    const std::string_view command = w[0];
    // after a press or a touch the buttons and the screen are let go for a frame, so the next one counts
    const Step release{Step::Kind::Wait, 1};

    if (command == "wait" && w.size() == 2)
    {
        const auto frames = number<int>(w[1]);
        if (!frames || *frames < 1)
            return false;
        out.push_back({Step::Kind::Wait, *frames});
        return true;
    }
    if ((command == "press" || command == "hold") && (w.size() == 2 || w.size() == 3))
    {
        const auto pressed = keys(w[1]);
        const auto frames = w.size() == 3 ? number<int>(w[2]) : std::optional<int>(1);
        if (!pressed || !frames || *frames < 1)
            return false;
        out.push_back({Step::Kind::Hold, *frames, *pressed});
        out.push_back(release);
        return true;
    }
    if (command == "touch" && (w.size() == 3 || w.size() == 4))
    {
        const auto x = number<float>(w[1]);
        const auto y = number<float>(w[2]);
        const auto frames = w.size() == 4 ? number<int>(w[3]) : std::optional<int>(1);
        if (!x || !y || !frames || *frames < 1)
            return false;
        out.push_back({Step::Kind::Touch, *frames, 0, *x, *y});
        out.push_back(release);
        return true;
    }
    if (command == "drag" && (w.size() == 5 || w.size() == 6))
    {
        const auto x = number<float>(w[1]);
        const auto y = number<float>(w[2]);
        const auto toX = number<float>(w[3]);
        const auto toY = number<float>(w[4]);
        const auto frames = w.size() == 6 ? number<int>(w[5]) : std::optional<int>(20);
        if (!x || !y || !toX || !toY || !frames || *frames < 2)
            return false;
        out.push_back({Step::Kind::Drag, *frames, 0, *x, *y, *toX, *toY});
        out.push_back(release);
        return true;
    }
    if (command == "shot" && w.size() == 2)
    {
        out.push_back({Step::Kind::Shot, 0, 0, 0, 0, 0, 0, std::string(w[1])});
        return true;
    }
    if (command == "log" && w.size() >= 2)
    {
        const size_t start = static_cast<size_t>(w[1].data() - line.data());
        out.push_back({Step::Kind::Log, 0, 0, 0, 0, 0, 0, std::string(line.substr(start))});
        return true;
    }
    if (command == "glyphs" && w.size() == 1)
    {
        out.push_back({Step::Kind::Glyphs});
        return true;
    }
    if (command == "dumpfont" && w.size() == 1)
    {
        out.push_back({Step::Kind::DumpFont});
        return true;
    }
    if (command == "savelog" && w.size() == 1)
    {
        out.push_back({Step::Kind::SaveLog});
        return true;
    }
    if (command == "quit" && w.size() == 1)
    {
        out.push_back({Step::Kind::Quit});
        return true;
    }
    if (command == "type" && w.size() >= 2)
    {
        // type <text>: the keyboard's next answer; "type -" cancels it
        const size_t start = static_cast<size_t>(w[1].data() - line.data());
        Step step{Step::Kind::Type};
        step.text = std::string(line.substr(start));
        while (!step.text.empty() && std::isspace(static_cast<unsigned char>(step.text.back())))
            step.text.pop_back();
        out.push_back(step);
        return true;
    }
    if (command == "mem" && w.size() == 1)
    {
        out.push_back({Step::Kind::Memory});
        return true;
    }
    if (command == "monkey" && w.size() >= 2 && w.size() <= 4)
    {
        // monkey <frames> [seed] [keys]: random presses, taps and drags
        const auto frames = number<int>(w[1]);
        const auto seed = w.size() >= 3 ? number<u32>(w[2]) : std::optional<u32>(1);
        const auto pressed = w.size() == 4 ? keys(w[3]) : std::optional<u32>(kMonkeyKeys);
        if (!frames || *frames < 1 || !seed || !pressed)
            return false;
        Step step{Step::Kind::Monkey, *frames, *pressed};
        step.x = static_cast<float>(*seed);
        out.push_back(step);
        out.push_back(release);
        return true;
    }
    if (command == "repeat" && w.size() == 2)
    {
        const auto times = number<int>(w[1]);
        if (!times || *times < 1)
            return false;
        repeats.emplace_back(*times, out.size());
        return true;
    }
    if (command == "end" && w.size() == 1 && !repeats.empty())
    {
        const auto [times, start] = repeats.back();
        repeats.pop_back();
        const std::vector<Step> block(out.begin() + static_cast<ptrdiff_t>(start), out.end());
        for (int i = 1; i < times; ++i)
            out.insert(out.end(), block.begin(), block.end());
        return true;
    }
    return false;
}

void logMemory()
{
    const struct mallinfo heap = mallinfo();
    LOG_INFO("autopilot: memory: heap %u KB in use, %u KB free in the arena; linear %lu KB free; GPU commands at most "
             "%d%%",
             static_cast<unsigned>(heap.uordblks / 1024), static_cast<unsigned>(heap.fordblks / 1024),
             static_cast<unsigned long>(linearSpaceFree() / 1024),
             static_cast<int>(gfx::takePeakCommandUse() * 100));
}

// A frame of the monkey: carries on with what it's doing, or picks something new.
void monkeyFrame(const Step& step, ui::RawInput& input)
{
    if (frame == 0)
    {
        monkey = Monkey{};
        monkey.random = static_cast<u32>(step.x) * 2654435761u + 1;
    }
    if (frame % 600 == 599)
        logMemory();
    if (monkey.left == 0 && monkey.gap > 0)
    {
        --monkey.gap;
        return;
    }
    if (monkey.left == 0)
    {
        const int kind = monkey.between(0, 99);
        monkey.keys = 0;
        monkey.touching = false;
        if (kind < 12)
        {
            // a tap anywhere on the touch screen
            monkey.touching = true;
            monkey.x = monkey.toX = static_cast<float>(monkey.between(0, 319));
            monkey.y = monkey.toY = static_cast<float>(monkey.between(0, 239));
            monkey.length = monkey.between(1, 6);
        }
        else if (kind < 18)
        {
            // a drag, mostly up or down like scrolling a list
            monkey.touching = true;
            monkey.x = static_cast<float>(monkey.between(10, 309));
            monkey.y = static_cast<float>(monkey.between(30, 210));
            monkey.toX = monkey.x + static_cast<float>(monkey.between(-40, 40));
            monkey.toY = static_cast<float>(monkey.between(0, 239));
            monkey.length = monkey.between(4, 20);
        }
        else
        {
            // a button: mostly a press, sometimes held long enough to repeat, sometimes two together
            std::vector<u32> allowed;
            for (u32 bit = 1; bit != 0; bit <<= 1)
                if (step.keys & bit)
                    allowed.push_back(bit);
            if (allowed.empty())
                return;
            monkey.keys = allowed[monkey.next() % allowed.size()];
            if (monkey.between(0, 9) == 0)
                monkey.keys |= allowed[monkey.next() % allowed.size()];
            monkey.length = monkey.between(0, 9) == 0 ? monkey.between(20, 50) : monkey.between(1, 3);
        }
        monkey.left = monkey.length;
        // mostly quick, now and then a pause to let things load
        monkey.gap = monkey.between(0, 19) == 0 ? monkey.between(30, 120) : monkey.between(1, 6);
    }
    input.held |= monkey.keys;
    if (monkey.touching)
    {
        const float t = monkey.length > 1 ? static_cast<float>(monkey.length - monkey.left) /
                                                static_cast<float>(monkey.length - 1)
                                          : 1.0f;
        input.held |= KEY_TOUCH;
        input.touching = true;
        input.x = monkey.x + (monkey.toX - monkey.x) * t;
        input.y = monkey.y + (monkey.toY - monkey.y) * t;
    }
    --monkey.left;
}

std::string outputPath(std::string_view name)
{
    return std::string(kOutputDir) + "/" + std::string(name);
}

// The console font's symbols, U+E000 to U+E0FF, with their codes: which of them the font has, and how they look.
class GlyphSheet final : public ui::Scene
{
public:
    void update(ui::Input& input, float) override
    {
        if (input.pressed(KEY_B))
            close();
    }
    void drawTop() override { draw(0xE000, gfx::metric::kTopWidth); }
    void drawBottom() override { draw(0xE080, gfx::metric::kBottomWidth); }

private:
    static void draw(char32_t first, float width)
    {
        using namespace gfx;
        fill(color::kBackground);
        const float labels = 34;
        const float cell = (width - labels) / 16;
        const float rowHeight = metric::kHeight / 9;
        char code[8];
        for (int column = 0; column < 16; ++column)
        {
            std::snprintf(code, sizeof(code), "%X", column);
            drawText(code, labels + column * cell + cell / 2, 2, metric::kTextSmall, color::kTextFaint, Align::Center);
        }
        for (int row = 0; row < 8; ++row)
        {
            const float y = rowHeight * (row + 1);
            std::snprintf(code, sizeof(code), "%04X", static_cast<unsigned>(first + row * 16));
            drawText(code, 2, y + 4, metric::kTextSmall, color::kTextFaint);
            for (int column = 0; column < 16; ++column)
            {
                const char32_t c = first + row * 16 + column;
                const char text[4] = {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
                                      static_cast<char>(0x80 | (c & 0x3F)), 0};
                drawText(text, labels + column * cell + cell / 2, y, metric::kTextNormal, color::kText, Align::Center);
            }
        }
    }
};

void dumpFont()
{
    // the shared font as the console maps it: 0x80 bytes of header before the font, what emulators load as
    // shared_font.bin
    fontEnsureMapped();
    const CFNT_s* font = fontGetSystemFont();
    if (!font)
    {
        LOG_WARN("autopilot: no system font");
        return;
    }
    const auto* start = reinterpret_cast<const uint8_t*>(font) - 0x80;
    const auto written = App::get().sd().writeFile(outputPath("shared_font.bin"), ByteSpan(start, 0x80 + font->fileSize));
    if (!written)
        LOG_WARN("autopilot: couldn't save the font: %s", written.error().detail.c_str());
}

void run(const Step& step)
{
    switch (step.kind)
    {
    case Step::Kind::Shot:
        shots.push_back(step.text);
        break;
    case Step::Kind::Log:
        LOG_INFO("autopilot: %s", step.text.c_str());
        break;
    case Step::Kind::Glyphs:
        App::get().scenes().emplace<GlyphSheet>();
        break;
    case Step::Kind::DumpFont:
        dumpFont();
        break;
    case Step::Kind::SaveLog:
        log::save(("sdmc:" + outputPath("log.txt")).c_str());
        break;
    case Step::Kind::Quit:
        quit = true;
        break;
    case Step::Kind::Memory:
        logMemory();
        break;
    case Step::Kind::Type:
        typed.push_back(step.text == "-" ? std::nullopt : std::optional<std::string>(step.text));
        break;
    default:
        break;
    }
}

// The done file goes with the frame after the script ends, once the screenshots asked for last are saved.
void finish()
{
    running = false;
    finished = true;
}

void writeDone()
{
    finished = false;
    (void)App::get().sd().writeFile(outputPath("done"), ByteSpan());
    LOG_INFO("autopilot: done");
}

} // namespace

void start()
{
    Archive& sd = App::get().sd();
    auto script = sd.readFile(kScriptPath, 256 * 1024);
    if (!script)
        return;
    const std::string_view text(reinterpret_cast<const char*>(script->data()), script->size());
    int number = 0;
    size_t at = 0;
    while (at <= text.size())
    {
        const size_t end = std::min(text.find('\n', at), text.size());
        ++number;
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!parse(line, steps))
            LOG_WARN("autopilot: line %d isn't a command: %.*s", number, static_cast<int>(line.size()), line.data());
        at = end + 1;
    }
    if (!repeats.empty())
        LOG_WARN("autopilot: %u repeat(s) without an end", static_cast<unsigned>(repeats.size()));
    (void)sd.createDirectories(kOutputDir);
    (void)sd.removeFile(outputPath("done"));
    running = !steps.empty();
    LOG_INFO("autopilot: %u steps", static_cast<unsigned>(steps.size()));
    if (running)
    {
        // the keyboard answers from the script (type), or for the monkey at random, never opening the system's
        ui::setKeyboardOverride([](const std::string& hint) -> std::optional<std::string> {
            std::optional<std::string> answer;
            if (!typed.empty())
            {
                answer = typed.front();
                typed.erase(typed.begin());
            }
            else if (monkeying)
            {
                static const char* const kWords[] = {"zelda", "pokemon", "a", "  ", "T1ED69", "S1EBAA", "BS1B98F",
                                                     "152151", "zzzzqqq", "\xc3\xa9t\xc3\xa9", "mario kart",
                                                     "%20&q=1", "x"};
                const u32 pick = monkey.next() % (std::size(kWords) + 2);
                if (pick < std::size(kWords))
                    answer = std::string(kWords[pick]);
            }
            LOG_INFO("autopilot: keyboard \"%s\" answers %s", hint.c_str(), answer ? answer->c_str() : "(cancel)");
            return answer;
        });
    }
}

void drive(ui::RawInput& input)
{
    if (!running)
        return;
    while (current < steps.size() && steps[current].frames == 0)
        run(steps[current++]);
    if (quit || current >= steps.size())
    {
        finish();
        return;
    }
    const Step& step = steps[current];
    const float t = step.frames > 1 ? static_cast<float>(frame) / static_cast<float>(step.frames - 1) : 1.0f;
    switch (step.kind)
    {
    case Step::Kind::Hold:
        input.held |= step.keys;
        break;
    case Step::Kind::Touch:
    case Step::Kind::Drag:
        input.held |= KEY_TOUCH;
        input.touching = true;
        input.x = step.kind == Step::Kind::Drag ? step.x + (step.toX - step.x) * t : step.x;
        input.y = step.kind == Step::Kind::Drag ? step.y + (step.toY - step.y) * t : step.y;
        break;
    case Step::Kind::Monkey:
        monkeying = frame + 1 < step.frames;
        monkeyFrame(step, input);
        break;
    default:
        break;
    }
    if (++frame >= step.frames)
    {
        frame = 0;
        ++current;
    }
}

void frameStarted()
{
    if (shots.empty())
    {
        if (finished)
            writeDone();
        return;
    }
    const auto png = encodePng(gfx::captureScreens());
    for (const std::string& name : shots)
    {
        const auto written = png ? App::get().sd().writeFile(outputPath(name + ".png"), *png)
                                 : Expected<void>(std::unexpected(png.error()));
        if (!written)
            LOG_WARN("autopilot: couldn't save %s: %s", name.c_str(), written.error().detail.c_str());
    }
    shots.clear();
    if (finished)
        writeDone();
}

bool keepsAppOpen()
{
    return running && monkeying;
}

bool quitting()
{
    // the screenshots asked for just before quit are taken first, and the done file written
    return quit && shots.empty() && !finished;
}

} // namespace tz::autopilot

#endif
