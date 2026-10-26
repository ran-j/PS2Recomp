#include "runtime/ps2_input_script.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ps2x
{
    namespace
    {
        bool parseStickToken(const std::string &token, uint8_t &out)
        {
            const size_t eq = token.find('=');
            if (eq == std::string::npos)
                return false;
            const int value = std::atoi(token.c_str() + eq + 1);
            const int clamped = std::clamp(value, -128, 127);
            out = static_cast<uint8_t>(128 + clamped);
            return true;
        }

        uint8_t stickCenter() { return 128u; }
    }

    bool LoadPadScript(const char *path, std::vector<PadScriptEvent> &out)
    {
        out.clear();
        if (path == nullptr)
            return false;

        std::ifstream in(path);
        if (!in)
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream fields(line);
            std::string token;
            bool hasTime = false;
            PadScriptEvent event;
            while (fields >> token)
            {
                if (token.rfind("t=", 0) == 0)
                {
                    event.tMs = static_cast<uint32_t>(std::strtoul(token.c_str() + 2, nullptr, 10));
                    hasTime = true;
                }
                else if (token.rfind("btns=", 0) == 0)
                {
                    event.btns = static_cast<uint16_t>(std::strtoul(token.c_str() + 5, nullptr, 16));
                }
                else if (token.rfind("lx=", 0) == 0)
                {
                    parseStickToken(token, event.lx);
                }
                else if (token.rfind("ly=", 0) == 0)
                {
                    parseStickToken(token, event.ly);
                }
                else if (token.rfind("rx=", 0) == 0)
                {
                    parseStickToken(token, event.rx);
                }
                else if (token.rfind("ry=", 0) == 0)
                {
                    parseStickToken(token, event.ry);
                }
            }
            if (hasTime)
                out.push_back(event);
        }

        if (out.empty())
            return false;

        std::sort(out.begin(), out.end(), [](const PadScriptEvent &a, const PadScriptEvent &b)
                  { return a.tMs < b.tMs; });
        return true;
    }

    void ApplyPadScript(const std::vector<PadScriptEvent> &events, uint32_t elapsedMs, uint8_t *data)
    {
        if (data == nullptr || events.empty())
            return;

        const PadScriptEvent *current = &events.front();
        for (const PadScriptEvent &event : events)
        {
            if (event.tMs <= elapsedMs)
                current = &event;
            else
                break;
        }

        std::fill(data, data + 32, 0);
        data[0] = 0x01;
        data[1] = 0x73; // analog marker
        const uint16_t wire = static_cast<uint16_t>(0xFFFFu & ~current->btns);
        data[2] = static_cast<uint8_t>(wire & 0xFF);
        data[3] = static_cast<uint8_t>(wire >> 8);
        data[4] = current->rx;
        data[5] = current->ry;
        data[6] = current->lx;
        data[7] = current->ly;
    }
}
