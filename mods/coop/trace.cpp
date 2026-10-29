#include "trace.h"

#include <cstdarg>
#include <cstdio>

#include "trace_rate.h"

namespace trace {

int g_detail = 1;
static const SkHost* H;
static SkMod* M;
static RateLimit g_rate(10.0);

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
}

static void write(const char* kind, const char* fmt, va_list ap)
{
    if (!H)
        return;
    char b[1024];
    vsnprintf(b, sizeof b, fmt, ap);
    H->log(M, SK_INFO, "%s: %s", kind, b);
}

void line(const char* kind, const char* fmt, ...)
{
    if (g_detail < 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    write(kind, fmt, ap);
    va_end(ap);
}

void verbose(const char* kind, const char* fmt, ...)
{
    if (g_detail < 2)
        return;
    va_list ap;
    va_start(ap, fmt);
    write(kind, fmt, ap);
    va_end(ap);
}

void ev(const char* kind, const char* key, const char* fmt, ...)
{
    if (g_detail < 1 || !H)
        return;
    std::string k = std::string(kind) + ": " + key;
    if (!g_rate.allow(k, H->time_seconds(), g_detail >= 2 ? 40 : 5))
        return;
    va_list ap;
    va_start(ap, fmt);
    write(kind, fmt, ap);
    va_end(ap);
}

void tick(double now)
{
    if (!H)
        return;
    for (auto& [k, n] : g_rate.take_summary(now))
        H->log(M, SK_INFO, "%s x%d more in the last %.0f s (%d in all)", k.c_str(), n.first, g_rate.window(), n.second);
}

std::string hex(uint32_t v)
{
    if (!v)
        return "-";
    char b[12];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

} // namespace trace
