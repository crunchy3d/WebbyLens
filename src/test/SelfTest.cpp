// SelfTest - loads WebbyLensCamera.dll directly (no registration needed),
// reads frames through an IMFSourceReader, measures fps and saves BMPs.
//   SelfTest.exe <path to WebbyLensCamera.dll> <output folder>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>
#include "../app/frame_writer.h"

using Microsoft::WRL::ComPtr;
DEFINE_GUID(CLSID_WebbyLensCamera, 0xc81b9576, 0x150f, 0x42b2, 0xa6, 0xb9, 0xac, 0xcb, 0x88, 0x8c, 0x26, 0x5b);

static void SaveBmp(const std::wstring& path, const std::vector<BYTE>& bgra, UINT w, UINT h)
{
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = (LONG)w; ih.biHeight = -(LONG)h; ih.biPlanes = 1; ih.biBitCount = 32;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + (DWORD)bgra.size();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return;
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(bgra.data(), 1, bgra.size(), f);
    fclose(f);
}

static BYTE Clip(int v) { return (BYTE)std::min(255, std::max(0, v)); }

static int ReadFormat(IMFMediaSource* source, DWORD typeIndex, const std::wstring& outFile)
{
    ComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 1);
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromMediaSource(source, ra.Get(), &reader);
    if (FAILED(hr)) { wprintf(L"  CreateSourceReader failed 0x%08X\n", hr); return 1; }

    ComPtr<IMFMediaType> type;
    hr = reader->GetNativeMediaType(0, typeIndex, &type);
    if (FAILED(hr)) { wprintf(L"  GetNativeMediaType(%u) failed 0x%08X\n", typeIndex, hr); return 1; }
    reader->SetStreamSelection(0, TRUE);
    hr = reader->SetCurrentMediaType(0, nullptr, type.Get());
    if (FAILED(hr)) { wprintf(L"  SetCurrentMediaType failed 0x%08X\n", hr); return 1; }

    GUID sub; UINT32 w = 0, h = 0;
    type->GetGUID(MF_MT_SUBTYPE, &sub);
    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
    bool nv12 = sub == MFVideoFormat_NV12;
    wprintf(L"  type %u: %s %ux%u\n", typeIndex, nv12 ? L"NV12" : L"RGB32", w, h);

    const int kFrames = 90;
    LONGLONG first = 0, last = 0, prevTs = 0, maxGap = 0;
    std::vector<BYTE> bgra;
    for (int i = 0; i < kFrames; i++)
    {
        DWORD stream, flags; LONGLONG ts; ComPtr<IMFSample> sample;
        hr = reader->ReadSample(0, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr)) { wprintf(L"  ReadSample failed 0x%08X at frame %d\n", hr, i); return 1; }
        if (!sample) { i--; continue; }
        LONGLONG now = MFGetSystemTime();
        if (i == 0) first = now;
        else maxGap = std::max(maxGap, ts - prevTs);
        prevTs = ts; last = now;

        if (i == kFrames - 1)
        {
            ComPtr<IMFMediaBuffer> buf;
            sample->ConvertToContiguousBuffer(&buf);
            BYTE* p; DWORD len;
            buf->Lock(&p, nullptr, &len);
            bgra.resize((size_t)w * h * 4);
            if (nv12)
            {
                const BYTE* Y = p; const BYTE* UV = p + (size_t)w * h;
                for (UINT y = 0; y < h; y++)
                    for (UINT x = 0; x < w; x++)
                    {
                        int c = Y[y * w + x] - 16, d = UV[(y / 2) * w + (x & ~1u)] - 128, e = UV[(y / 2) * w + (x & ~1u) + 1] - 128;
                        BYTE* o = &bgra[((size_t)y * w + x) * 4];
                        o[2] = Clip((298 * c + 409 * e + 128) >> 8);
                        o[1] = Clip((298 * c - 100 * d - 208 * e + 128) >> 8);
                        o[0] = Clip((298 * c + 516 * d + 128) >> 8);
                        o[3] = 255;
                    }
            }
            else
            {
                memcpy(bgra.data(), p, std::min<size_t>(bgra.size(), len));
            }
            buf->Unlock();
            wprintf(L"  last sample: %u bytes\n", len);
        }
    }
    double secs = (last - first) / 1e7;
    wprintf(L"  %d frames in %.2f s = %.1f fps, max timestamp gap %.1f ms\n",
        kFrames - 1, secs, (kFrames - 1) / secs, maxGap / 1e4);
    SaveBmp(outFile, bgra, w, h);
    wprintf(L"  saved %s\n", outFile.c_str());
    return 0;
}

// Publishes synthetic "phone" frames through the shared file, like WebbyLens.exe does.
static std::atomic<bool> g_feedStop{ false };
static void Feed(UINT32 w, UINT32 h)
{
    FrameWriter writer;
    std::string err;
    if (!writer.Open(&err)) { printf("  feed: %s\n", err.c_str()); return; }
    std::vector<BYTE> buf((size_t)w * h * 3 / 2);
    BYTE* Y = buf.data();
    BYTE* UV = Y + (size_t)w * h;
    for (UINT32 y = 0; y < h; y++)
        for (UINT32 x = 0; x < w; x++)
        {
            bool border = x < 16 || y < 16 || x >= w - 16 || y >= h - 16;
            Y[(size_t)y * w + x] = border ? 235 : (BYTE)(60 + 140 * x / w);
        }
    for (UINT32 y = 0; y < h / 2; y++)
        for (UINT32 x = 0; x < w / 2; x++)
        {
            UV[(size_t)y * w + 2 * x] = (BYTE)(68 + 120 * y / (h / 2));     // U: blue towards bottom
            UV[(size_t)y * w + 2 * x + 1] = (BYTE)(68 + 120 * x / (w / 2)); // V: red towards right
        }
    Nv12Frame f{ Y, UV, (LONG)w, w, h, 0 };
    while (!g_feedStop)
    {
        writer.Write(f);
        Sleep(33);
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"Usage: SelfTest <WebbyLensCamera.dll> <outdir> [WxH feed]\n"); return 1; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    std::thread feeder;
    if (argc > 3)
    {
        UINT32 fw = 0, fh = 0;
        swscanf_s(argv[3], L"%ux%u", &fw, &fh);
        wprintf(L"Feeding synthetic %ux%u phone frames\n", fw, fh);
        feeder = std::thread(Feed, fw, fh);
        Sleep(300);
    }

    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { wprintf(L"LoadLibrary failed %u\n", GetLastError()); return 1; }
    auto getClass = (HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**))GetProcAddress(dll, "DllGetClassObject");

    ComPtr<IClassFactory> factory;
    HRESULT hr = getClass(CLSID_WebbyLensCamera, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { wprintf(L"DllGetClassObject failed 0x%08X\n", hr); return 1; }

    int rc = 0;
    DWORD types[] = { 0, 1, 2 }; // NV12 1920x1080, NV12 1280x720, RGB32 1280x720
    for (DWORD t : types)
    {
        ComPtr<IMFActivate> activate;
        hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&activate));
        if (FAILED(hr)) { wprintf(L"CreateInstance failed 0x%08X\n", hr); return 1; }
        ComPtr<IMFMediaSource> source;
        hr = activate->ActivateObject(IID_PPV_ARGS(&source));
        if (FAILED(hr)) { wprintf(L"ActivateObject failed 0x%08X\n", hr); return 1; }

        std::wstring out = std::wstring(argv[2]) +
            (t == 0 ? L"\\frame_nv12_1080.bmp" : t == 1 ? L"\\frame_nv12_720.bmp" : L"\\frame_rgb32_720.bmp");
        rc |= ReadFormat(source.Get(), t, out);
        source->Shutdown();
    }

    g_feedStop = true;
    if (feeder.joinable()) feeder.join();
    MFShutdown();
    CoUninitialize();
    wprintf(rc ? L"SELFTEST FAILED\n" : L"SELFTEST OK\n");
    return rc;
}
