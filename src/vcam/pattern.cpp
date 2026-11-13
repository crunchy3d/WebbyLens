// Test picture: color bars, a moving box and a running clock (HH:MM:SS.cc).
// The clock lets you judge on the far end of a call whether video is live
// and roughly how large the delay is.
#include "common.h"
#include <algorithm>

namespace
{
    struct Canvas
    {
        bool nv12;
        BYTE* p;      // first (top) row
        LONG pitch;   // may be negative for bottom-up RGB
        int w, h;

        void Fill(int x, int y, int cw, int ch, BYTE r, BYTE g, BYTE b) const
        {
            int x0 = std::max(0, x), y0 = std::max(0, y);
            int x1 = std::min(w, x + cw), y1 = std::min(h, y + ch);
            if (x0 >= x1 || y0 >= y1)
                return;

            if (!nv12)
            {
                UINT32 px = 0xFF000000u | (UINT32(r) << 16) | (UINT32(g) << 8) | b;
                for (int yy = y0; yy < y1; yy++)
                {
                    UINT32* row = reinterpret_cast<UINT32*>(p + (LONG_PTR)yy * pitch);
                    std::fill(row + x0, row + x1, px);
                }
                return;
            }

            // BT.601 limited range
            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            for (int yy = y0; yy < y1; yy++)
                memset(p + (LONG_PTR)yy * pitch + x0, Y, x1 - x0);

            BYTE* uv = p + (LONG_PTR)pitch * h;
            for (int cy = y0 / 2; cy < (y1 + 1) / 2; cy++)
            {
                BYTE* row = uv + (LONG_PTR)cy * pitch;
                for (int cx = x0 / 2; cx < (x1 + 1) / 2; cx++)
                {
                    row[cx * 2] = (BYTE)U;
                    row[cx * 2 + 1] = (BYTE)V;
                }
            }
        }
    };

    // Seven-segment digits: bit0=a(top) b c d(bottom) e f g(middle)
    const BYTE kSegments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

    void DrawDigit(const Canvas& c, int x, int y, int dw, int dh, int digit, BYTE r, BYTE g, BYTE b)
    {
        int t = std::max(2, dw / 6);
        int half = dh / 2;
        BYTE s = kSegments[digit % 10];
        if (s & 0x01) c.Fill(x + t, y, dw - 2 * t, t, r, g, b);                        // a
        if (s & 0x02) c.Fill(x + dw - t, y + t, t, half - t, r, g, b);                 // b
        if (s & 0x04) c.Fill(x + dw - t, y + half, t, half - t, r, g, b);              // c
        if (s & 0x08) c.Fill(x + t, y + dh - t, dw - 2 * t, t, r, g, b);               // d
        if (s & 0x10) c.Fill(x, y + half, t, half - t, r, g, b);                       // e
        if (s & 0x20) c.Fill(x, y + t, t, half - t, r, g, b);                          // f
        if (s & 0x40) c.Fill(x + t, y + half - t / 2, dw - 2 * t, t, r, g, b);         // g
    }
}

// "Paused" picture: pause symbol and the word PAUSED (5x7 pixel font).
void TestPattern::RenderPaused(const VideoFormat& fmt, BYTE* scan0, LONG pitch)
{
    Canvas c{ fmt.subtype == MFVideoFormat_NV12, scan0, pitch, (int)fmt.width, (int)fmt.height };
    const int w = c.w, h = c.h;
    c.Fill(0, 0, w, h, 28, 30, 36);

    int ref = std::min(h, w * 9 / 16);
    // pause symbol: two rounded-looking bars
    int barH = ref * 22 / 100, barW = barH / 3, gap = barW * 2 / 3;
    int sx = (w - (2 * barW + gap)) / 2, sy = h * 30 / 100 - barH / 2;
    const BYTE g = 200;
    c.Fill(sx, sy, barW, barH, g, g, g);
    c.Fill(sx + barW + gap, sy, barW, barH, g, g, g);

    // PAUSED
    static const char* glyphs[6][7] = {
        { "11110", "10001", "10001", "11110", "10000", "10000", "10000" },  // P
        { "01110", "10001", "10001", "11111", "10001", "10001", "10001" },  // A
        { "10001", "10001", "10001", "10001", "10001", "10001", "01110" },  // U
        { "01111", "10000", "10000", "01110", "00001", "00001", "11110" },  // S
        { "11111", "10000", "10000", "11110", "10000", "10000", "11111" },  // E
        { "11110", "10001", "10001", "10001", "10001", "10001", "11110" },  // D
    };
    int px = std::max(2, ref / 70);                    // size of one font pixel
    int charW = 5 * px, space = 2 * px;
    int textW = 6 * charW + 5 * space;
    int tx = (w - textW) / 2, ty = h * 62 / 100;
    for (int ch = 0; ch < 6; ch++)
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (glyphs[ch][row][col] == '1')
                    c.Fill(tx + ch * (charW + space) + col * px, ty + row * px, px, px, g, g, g);
}

// Calm dark picture with a phone outline, shown while no phone frames arrive.
void TestPattern::RenderNoSignal(const VideoFormat& fmt, BYTE* scan0, LONG pitch)
{
    Canvas c{ fmt.subtype == MFVideoFormat_NV12, scan0, pitch, (int)fmt.width, (int)fmt.height };
    const int w = c.w, h = c.h;
    c.Fill(0, 0, w, h, 28, 30, 36);

    int ref = std::min(h, w * 9 / 16);
    int ph = ref * 40 / 100, pw = ph * 52 / 100;      // phone body
    int px = (w - pw) / 2, py = (h - ph) / 2;
    int t = std::max(2, ref / 90);                      // outline thickness
    const BYTE g = 120;
    c.Fill(px, py, pw, t, g, g, g);
    c.Fill(px, py + ph - t, pw, t, g, g, g);
    c.Fill(px, py, t, ph, g, g, g);
    c.Fill(px + pw - t, py, t, ph, g, g, g);
    int cam = std::max(4, ref / 45);                    // camera dot
    c.Fill(px + (pw - cam) / 2, py + ph / 12, cam, cam, g, g, g);

    // diagonal slash
    int steps = ph / std::max(2, t);
    for (int i = 0; i < steps; i++)
    {
        int x = px - pw / 4 + (pw + pw / 2) * i / steps;
        int y = py - ph / 10 + (ph + ph / 5) * i / steps;
        c.Fill(x, y, t * 2, t * 2, 200, 70, 60);
    }
}

void TestPattern::Render(const VideoFormat& fmt, BYTE* scan0, LONG pitch, UINT64 frame)
{
    Canvas c{ fmt.subtype == MFVideoFormat_NV12, scan0, pitch, (int)fmt.width, (int)fmt.height };
    const int w = c.w, h = c.h;

    // background
    c.Fill(0, 0, w, h, 24, 24, 32);

    // 75% color bars in the top 45%
    static const BYTE bars[8][3] = {
        {191,191,191}, {191,191,0}, {0,191,191}, {0,191,0},
        {191,0,191}, {191,0,0}, {0,0,191}, {16,16,16} };
    int barsH = h * 45 / 100;
    for (int i = 0; i < 8; i++)
    {
        int x0 = w * i / 8, x1 = w * (i + 1) / 8;
        c.Fill(x0, 0, x1 - x0, barsH, bars[i][0], bars[i][1], bars[i][2]);
    }

    // moving box: one sweep every 4 seconds, based on wall clock
    LONGLONG t = MFGetSystemTime();
    int box = h / 10;
    int travel = w - box;
    LONGLONG phase = (t / 10000) % 4000; // ms within 4 s
    int bx = (int)(travel * phase / 4000);
    c.Fill(0, barsH + h / 50, w, box, 48, 48, 60);
    c.Fill(bx, barsH + h / 50, box, box, 255, 255, 255);

    // clock HH:MM:SS.cc
    SYSTEMTIME st;
    GetLocalTime(&st);
    int digits[8] = { st.wHour / 10, st.wHour % 10, st.wMinute / 10, st.wMinute % 10,
                      st.wSecond / 10, st.wSecond % 10, st.wMilliseconds / 100, (st.wMilliseconds / 10) % 10 };

    int ref = std::min(h, w * 9 / 16);    // size relative to a 16:9 frame
    int dh = ref * 22 / 100;
    int dw = dh * 55 / 100;
    int gap = dh / 8;
    int sep = dh / 4;                      // width reserved for ':' or '.'
    int total = 8 * dw + 7 * gap + 3 * sep;
    int x = (w - total) / 2;
    int y = barsH + h / 50 + box + (h - (barsH + h / 50 + box) - dh) / 2;
    int dot = std::max(2, dw / 6);

    for (int i = 0; i < 8; i++)
    {
        DrawDigit(c, x, y, dw, dh, digits[i], 255, 170, 0);
        x += dw + gap;
        if (i == 1 || i == 3)
        {
            c.Fill(x + (sep - dot) / 2 - gap / 2, y + dh / 3 - dot / 2, dot, dot, 255, 170, 0);
            c.Fill(x + (sep - dot) / 2 - gap / 2, y + 2 * dh / 3 - dot / 2, dot, dot, 255, 170, 0);
            x += sep;
        }
        else if (i == 5)
        {
            c.Fill(x + (sep - dot) / 2 - gap / 2, y + dh - dot, dot, dot, 255, 170, 0);
            x += sep;
        }
    }

    // format marker bottom-left: green = NV12, blue = RGB32
    int m = std::max(8, h / 30);
    if (c.nv12)
        c.Fill(m / 2, h - m - m / 2, m, m, 0, 200, 0);
    else
        c.Fill(m / 2, h - m - m / 2, m, m, 0, 80, 255);

    // frame parity marker bottom-right (blinks every frame: shows dropped frames as uneven flicker)
    if (frame & 1)
        c.Fill(w - m - m / 2, h - m - m / 2, m, m, 255, 255, 255);
}
