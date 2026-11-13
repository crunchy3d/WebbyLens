// High-quality image plane resampling (separable).
//  - shrinking: area averaging (every source pixel contributes according to its
//    covered area) -> no aliasing / "combing" when e.g. 1920x1080 -> 640x360
//  - enlarging: bilinear
// Works on planes with 1 (Y) or 2 interleaved (UV) channels.
#pragma once
#include <windows.h>
#include <cmath>
#include <vector>

class ResampleAxis
{
public:
    std::vector<int> start;    // first source index for each destination index
    std::vector<int> count;    // number of taps
    std::vector<int> offset;   // offset into weights
    std::vector<int> weights;  // fixed point, sum = 1 << 14 per destination index

    void Build(int src, int dst)
    {
        if (src == _src && dst == _dst)
            return;
        _src = src; _dst = dst;
        start.assign(dst, 0); count.assign(dst, 0); offset.assign(dst, 0);
        weights.clear();
        const double scale = (double)src / dst;
        for (int d = 0; d < dst; d++)
        {
            offset[d] = (int)weights.size();
            if (dst < src)
            {
                double s0 = d * scale, s1 = (d + 1) * scale;
                int i0 = (int)floor(s0), i1 = (int)ceil(s1);
                if (i1 > src) i1 = src;
                int sum = 0;
                for (int i = i0; i < i1; i++)
                {
                    double a = (i + 1 < s1 ? i + 1 : s1) - (i > s0 ? i : s0);
                    int w = (int)(a / scale * 16384 + 0.5);
                    weights.push_back(w);
                    sum += w;
                }
                weights.back() += 16384 - sum;   // exact normalisation
                start[d] = i0;
                count[d] = i1 - i0;
            }
            else
            {
                double pos = (d + 0.5) * scale - 0.5;
                if (pos < 0) pos = 0;
                int i0 = (int)pos;
                int w1 = (int)((pos - i0) * 16384 + 0.5);
                if (i0 >= src - 1) { i0 = src - 1; w1 = 0; }
                start[d] = i0;
                if (w1 == 0) { count[d] = 1; weights.push_back(16384); }
                else { count[d] = 2; weights.push_back(16384 - w1); weights.push_back(w1); }
            }
        }
    }

private:
    int _src = 0, _dst = 0;
};

class Resampler
{
public:
    void Plane(const BYTE* src, int sw, int sh, LONG sstride,
               BYTE* dst, int dw, int dh, LONG dstride, int ch)
    {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
            return;
        if (sw == dw && sh == dh)
        {
            for (int y = 0; y < dh; y++)
                memcpy(dst + (LONG_PTR)y * dstride, src + (LONG_PTR)y * sstride, (size_t)dw * ch);
            return;
        }
        _ax.Build(sw, dw);
        _ay.Build(sh, dh);
        const int rowLen = dw * ch;

        // Horizontal pass: only the source rows that the vertical pass needs (all of them here).
        _tmp.resize((size_t)sh * rowLen);
        for (int y = 0; y < sh; y++)
        {
            const BYTE* in = src + (LONG_PTR)y * sstride;
            BYTE* out = _tmp.data() + (size_t)y * rowLen;
            for (int x = 0; x < dw; x++)
            {
                const int* w = &_ax.weights[_ax.offset[x]];
                const BYTE* p = in + (size_t)_ax.start[x] * ch;
                int n = _ax.count[x];
                for (int c = 0; c < ch; c++)
                {
                    int sum = 8192;
                    for (int k = 0; k < n; k++)
                        sum += w[k] * p[k * ch + c];
                    out[x * ch + c] = (BYTE)(sum >> 14);
                }
            }
        }

        // Vertical pass
        _acc.resize(rowLen);
        for (int y = 0; y < dh; y++)
        {
            const int* w = &_ay.weights[_ay.offset[y]];
            int n = _ay.count[y];
            const BYTE* rows = _tmp.data() + (size_t)_ay.start[y] * rowLen;
            for (int i = 0; i < rowLen; i++) _acc[i] = 8192;
            for (int k = 0; k < n; k++)
            {
                const BYTE* r = rows + (size_t)k * rowLen;
                int wk = w[k];
                for (int i = 0; i < rowLen; i++)
                    _acc[i] += wk * r[i];
            }
            BYTE* out = dst + (LONG_PTR)y * dstride;
            for (int i = 0; i < rowLen; i++)
            {
                int v = _acc[i] >> 14;
                out[i] = (BYTE)(v > 255 ? 255 : v);
            }
        }
    }

private:
    ResampleAxis _ax, _ay;
    std::vector<BYTE> _tmp;
    std::vector<int> _acc;
};
