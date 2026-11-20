#include "QrCode.h"

namespace
{
    // Niveau M, versions 1 à 6 (index 0 inutilisé).
    constexpr int eccPerBlock[] = { -1, 10, 16, 26, 18, 24, 16 };
    constexpr int numBlocks[]   = { -1, 1, 1, 1, 2, 2, 4 };
    constexpr int maxVersion = 6;
    constexpr int formatBitsM = 0;     // L = 1, M = 0, Q = 3, H = 2

    int rawDataModules (int v)
    {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) { const int na = v / 7 + 2; r -= (25 * na - 10) * na - 55; if (v >= 7) r -= 36; }
        return r;
    }

    int dataCodewords (int v) { return rawDataModules (v) / 8 - eccPerBlock[v] * numBlocks[v]; }

    uint8_t gfMul (uint8_t x, uint8_t y)
    {
        int z = 0;
        for (int i = 7; i >= 0; --i)
        {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return (uint8_t) z;
    }

    std::vector<uint8_t> rsDivisor (int degree)
    {
        std::vector<uint8_t> r ((size_t) degree, 0);
        r[(size_t) degree - 1] = 1;
        uint8_t root = 1;
        for (int i = 0; i < degree; ++i)
        {
            for (size_t j = 0; j < r.size(); ++j)
            {
                r[j] = gfMul (r[j], root);
                if (j + 1 < r.size()) r[j] ^= r[j + 1];
            }
            root = gfMul (root, 0x02);
        }
        return r;
    }

    std::vector<uint8_t> rsRemainder (const std::vector<uint8_t>& data, const std::vector<uint8_t>& div)
    {
        std::vector<uint8_t> r (div.size(), 0);
        for (auto b : data)
        {
            const uint8_t factor = b ^ r[0];
            r.erase (r.begin());
            r.push_back (0);
            for (size_t i = 0; i < r.size(); ++i) r[i] ^= gfMul (div[i], factor);
        }
        return r;
    }

    std::vector<int> alignmentPositions (int v)
    {
        if (v == 1) return {};
        const int na = v / 7 + 2, size = v * 4 + 17;
        const int step = (v * 4 + na * 2 + 1) / (na * 2 - 2) * 2;
        std::vector<int> r ((size_t) na);
        r[0] = 6;
        for (int i = na - 1, pos = size - 7; i >= 1; --i, pos -= step) r[(size_t) i] = pos;
        return r;
    }
}

void QrCode::setFunction (int x, int y, bool d)
{
    modules[(size_t) (y * n + x)] = d;
    isFunction[(size_t) (y * n + x)] = true;
}

void QrCode::drawFunctionPatterns()
{
    for (int i = 0; i < n; ++i) { setFunction (6, i, i % 2 == 0); setFunction (i, 6, i % 2 == 0); }
    auto finder = [this] (int cx, int cy)
    {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx)
            {
                const int d = std::max (std::abs (dx), std::abs (dy)), x = cx + dx, y = cy + dy;
                if (x >= 0 && x < n && y >= 0 && y < n) setFunction (x, y, d != 2 && d != 4);
            }
    };
    finder (3, 3); finder (n - 4, 3); finder (3, n - 4);
    const auto al = alignmentPositions (ver);
    const int na = (int) al.size();
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < na; ++j)
        {
            if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    setFunction (al[(size_t) i] + dx, al[(size_t) j] + dy, std::max (std::abs (dx), std::abs (dy)) != 1);
        }
    drawFormatBits (0);    // réserve les zones de format (réécrites avec le vrai masque)
}

void QrCode::drawFormatBits (int mask)
{
    const int data = formatBitsM << 3 | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = (data << 10 | rem) ^ 0x5412;
    auto bit = [bits] (int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) setFunction (8, i, bit (i));
    setFunction (8, 7, bit (6));
    setFunction (8, 8, bit (7));
    setFunction (7, 8, bit (8));
    for (int i = 9; i < 15; ++i) setFunction (14 - i, 8, bit (i));
    for (int i = 0; i < 8; ++i) setFunction (n - 1 - i, 8, bit (i));
    for (int i = 8; i < 15; ++i) setFunction (8, n - 15 + i, bit (i));
    setFunction (8, n - 8, true);   // module toujours sombre
}

void QrCode::drawCodewords (const std::vector<uint8_t>& data)
{
    size_t i = 0;
    for (int right = n - 1; right >= 1; right -= 2)
    {
        if (right == 6) right = 5;
        for (int vert = 0; vert < n; ++vert)
            for (int j = 0; j < 2; ++j)
            {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? n - 1 - vert : vert;
                if (! isFunction[(size_t) (y * n + x)] && i < data.size() * 8)
                {
                    modules[(size_t) (y * n + x)] = ((data[i >> 3] >> (7 - (int) (i & 7))) & 1) != 0;
                    ++i;
                }
            }
    }
}

void QrCode::applyMask (int mask)
{
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            bool inv = false;
            switch (mask)
            {
                case 0: inv = (x + y) % 2 == 0; break;
                case 1: inv = y % 2 == 0; break;
                case 2: inv = x % 3 == 0; break;
                case 3: inv = (x + y) % 3 == 0; break;
                case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
                case 5: inv = x * y % 2 + x * y % 3 == 0; break;
                case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            const size_t k = (size_t) (y * n + x);
            if (inv && ! isFunction[k]) modules[k] = ! modules[k];
        }
}

long QrCode::penalty() const
{
    // Règles 1 (séries), 2 (blocs 2x2), 3 (motifs semblables aux repères) et 4 (équilibre).
    long r = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int a = 0; a < n; ++a)
        {
            int run = 0; bool prev = false;
            std::vector<bool> line ((size_t) n);
            for (int b = 0; b < n; ++b)
            {
                const bool c = pass == 0 ? dark (b, a) : dark (a, b);
                line[(size_t) b] = c;
                if (b > 0 && c == prev) { if (++run == 5) r += 3; else if (run > 5) ++r; }
                else run = 1;
                prev = c;
            }
            for (int b = 0; b + 10 < n + 1; ++b)
            {
                static const bool p1[] = { true, false, true, true, true, false, true, false, false, false, false };
                static const bool p2[] = { false, false, false, false, true, false, true, true, true, false, true };
                bool m1 = true, m2 = true;
                for (int k = 0; k < 11; ++k)
                {
                    if (b + k >= n) { m1 = m2 = false; break; }
                    if (line[(size_t) (b + k)] != p1[k]) m1 = false;
                    if (line[(size_t) (b + k)] != p2[k]) m2 = false;
                }
                if (m1 || m2) r += 40;
            }
        }
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x)
        {
            const bool c = dark (x, y);
            if (c == dark (x + 1, y) && c == dark (x, y + 1) && c == dark (x + 1, y + 1)) r += 3;
        }
    long darkCount = 0;
    for (bool m : modules) if (m) ++darkCount;
    const long total = (long) n * n;
    const long k = (std::abs (darkCount * 20 - total * 10) + total - 1) / total - 1;
    r += std::max (0L, k) * 10;
    return r;
}

QrCode QrCode::encode (const juce::String& text)
{
    QrCode q;
    const auto utf8 = text.toStdString();
    int v = 1;
    while (v <= maxVersion && (int) utf8.size() + 2 > dataCodewords (v)) ++v;   // 4 + 8 bits d'en-tête ≈ 2 octets
    if (v > maxVersion) return q;

    // Flux de bits : mode octets (0100), longueur sur 8 bits (versions 1 à 9), données.
    std::vector<bool> bits;
    auto append = [&bits] (uint32_t val, int len) { for (int i = len - 1; i >= 0; --i) bits.push_back (((val >> i) & 1) != 0); };
    append (4, 4);
    append ((uint32_t) utf8.size(), 8);
    for (unsigned char c : utf8) append (c, 8);
    const size_t capacity = (size_t) dataCodewords (v) * 8;
    append (0, (int) std::min<size_t> (4, capacity - bits.size()));
    append (0, (int) ((8 - bits.size() % 8) % 8));
    for (uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11) append (pad, 8);
    std::vector<uint8_t> data (bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) data[i >> 3] |= (uint8_t) ((bits[i] ? 1 : 0) << (7 - (int) (i & 7)));

    // Blocs, correction d'erreur Reed-Solomon et entrelacement.
    const int nb = numBlocks[v], eccLen = eccPerBlock[v], raw = rawDataModules (v) / 8;
    const int numShort = nb - raw % nb, shortLen = raw / nb;
    const auto div = rsDivisor (eccLen);
    std::vector<std::vector<uint8_t>> blocks;
    for (int i = 0, k = 0; i < nb; ++i)
    {
        const int len = shortLen - eccLen + (i < numShort ? 0 : 1);
        std::vector<uint8_t> dat (data.begin() + k, data.begin() + k + len);
        k += len;
        const auto ecc = rsRemainder (dat, div);
        if (i < numShort) dat.push_back (0);
        dat.insert (dat.end(), ecc.begin(), ecc.end());
        blocks.push_back (dat);
    }
    std::vector<uint8_t> all;
    for (size_t i = 0; i < blocks[0].size(); ++i)
        for (int j = 0; j < nb; ++j)
            if ((int) i != shortLen - eccLen || j >= numShort) all.push_back (blocks[(size_t) j][i]);

    q.ver = v;
    q.n = v * 4 + 17;
    q.modules.assign ((size_t) (q.n * q.n), false);
    q.isFunction.assign ((size_t) (q.n * q.n), false);
    q.drawFunctionPatterns();
    q.drawCodewords (all);

    // Masque de pénalité minimale.
    int best = 0; long bestPenalty = -1;
    for (int m = 0; m < 8; ++m)
    {
        q.applyMask (m); q.drawFormatBits (m);
        const long p = q.penalty();
        if (bestPenalty < 0 || p < bestPenalty) { bestPenalty = p; best = m; }
        q.applyMask (m);    // le masque est son propre inverse
    }
    q.applyMask (best);
    q.drawFormatBits (best);
    return q;
}

void QrCode::draw (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (juce::Colours::white);
    g.fillRect (area);
    if (n == 0) return;
    const float cell = juce::jmin (area.getWidth(), area.getHeight()) / (float) (n + 8);
    const float x0 = area.getX() + (area.getWidth() - cell * (float) n) * 0.5f, y0 = area.getY() + (area.getHeight() - cell * (float) n) * 0.5f;
    g.setColour (juce::Colours::black);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (dark (x, y)) g.fillRect (x0 + (float) x * cell, y0 + (float) y * cell, cell + 0.5f, cell + 0.5f);
}
