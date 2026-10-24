// [Systems] Voice capture on Windows (ONLINE_MP_ARCHITECTURE.md section 7): WASAPI shared-mode capture of the default communications
// microphone (eCommunications, falling back to eConsole), event driven on its own thread, the device's mix format (float or 16-bit
// PCM, any rate / channels) handed to net::VoiceFramer -> mono 16 kHz 20 ms frames. Opened only while the session wants voice
// (push-to-talk held / open mic on) and the user enabled it; nothing is recorded to disk.
#include "net/VoiceFrames.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "core/Log.h"

namespace net {
namespace {

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// PKEY_Device_FriendlyName {a45c254e-df1c-4efd-8020-67d146a850e0}, 14 (defined here: no INITGUID / extra import library)
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string utf8(const wchar_t* w) {
    if (!w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// Every active capture endpoint: (friendly name, device). The caller releases the devices.
std::vector<std::pair<std::string, IMMDevice*>> captureEndpoints(IMMDeviceEnumerator* en) {
    std::vector<std::pair<std::string, IMMDevice*>> out;
    IMMDeviceCollection* col = nullptr;
    if (!en || FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d))) continue;
        IPropertyStore* ps = nullptr;
        std::string name;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) name = utf8(v.pwszVal);
            PropVariantClear(&v);
            release(ps);
        }
        if (name.empty()) { release(d); continue; }
        out.emplace_back(name, d);
    }
    release(col);
    return out;
}

class Win32VoiceCapture final : public IVoiceCapture {
public:
    explicit Win32VoiceCapture(std::string device) : device_(std::move(device)) {}
    ~Win32VoiceCapture() override { stop(); }
    bool start() override {
        if (running_) return true;
        stopFlag_ = false;
        ready_ = 0;
        thread_ = std::thread([this] { run(); });
        while (ready_ == 0) Sleep(1);   // the thread reports whether the device opened
        if (ready_ < 0) { if (thread_.joinable()) thread_.join(); return false; }
        running_ = true;
        return true;
    }
    void stop() override {
        stopFlag_ = true;
        if (thread_.joinable()) thread_.join();
        running_ = false;
    }
    bool running() const override { return running_; }
    bool pop(VoiceFrame& out) override { return framer_.pop(out); }
    float level() const override { return framer_.level(); }

private:
    void run() {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr; IAudioCaptureClient* cc = nullptr;
        WAVEFORMATEX* wf = nullptr;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en));
        if (ok && !device_.empty()) {   // the Input Device row's endpoint by name; gone (unplugged) -> the default below
            for (auto& e : captureEndpoints(en)) {
                if (!dev && e.first == device_) dev = e.second;
                else release(e.second);
            }
            if (!dev) LOG_WARN("voice capture: input device '%s' not found - using the default", device_.c_str());
        }
        if (ok && !dev && FAILED(en->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev))) ok = SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev));
        ok = ok && dev && SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac));
        ok = ok && SUCCEEDED(ac->GetMixFormat(&wf));
        ok = ok && SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000 /* 20 ms */, 0, wf, nullptr));
        ok = ok && SUCCEEDED(ac->SetEventHandle(ev));
        ok = ok && SUCCEEDED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc));
        bool isFloat = false;
        if (ok) {
            isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)wf;
                isFloat = x->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;   // the KSDATAFORMAT subtype GUID's Data1 is the format tag
            }
            ok = isFloat ? wf->wBitsPerSample == 32 : wf->wBitsPerSample == 16;
            if (!ok) LOG_WARN("voice capture: unsupported mix format (%d bits, float %d)", wf->wBitsPerSample, (int)isFloat);
        }
        ok = ok && SUCCEEDED(ac->Start());
        if (ok) {
            framer_.configure((int)wf->nSamplesPerSec, (int)wf->nChannels);
            LOG_INFO("voice capture: started (%u Hz, %u ch, %s)", (unsigned)wf->nSamplesPerSec, (unsigned)wf->nChannels, isFloat ? "float" : "pcm16");
        } else {
            LOG_WARN("voice capture: no capture device could be opened (voice send unavailable)");
        }
        ready_ = ok ? 1 : -1;
        std::vector<float> conv;
        while (ok && !stopFlag_) {
            if (WaitForSingleObject(ev, 100) != WAIT_OBJECT_0) continue;
            UINT32 n = 0;
            while (SUCCEEDED(cc->GetNextPacketSize(&n)) && n > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                const size_t samples = (size_t)frames * wf->nChannels;
                conv.assign(samples, 0.0f);
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    if (isFloat) { const float* f = (const float*)data; for (size_t i = 0; i < samples; ++i) conv[i] = f[i]; }
                    else { const int16_t* s = (const int16_t*)data; for (size_t i = 0; i < samples; ++i) conv[i] = (float)s[i] / 32768.0f; }
                }
                framer_.push(conv.data(), frames);
                cc->ReleaseBuffer(frames);
            }
        }
        if (ac) ac->Stop();
        if (wf) CoTaskMemFree(wf);
        release(cc); release(ac); release(dev); release(en);
        if (ev) CloseHandle(ev);
        if (com) CoUninitialize();
        if (ok) LOG_INFO("voice capture: stopped");
    }
    std::string device_;
    VoiceFramer framer_;
    std::thread thread_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<int> ready_{0};
    bool running_ = false;
};

}  // namespace

std::unique_ptr<IVoiceCapture> createVoiceCapture(const std::string& device) { return std::make_unique<Win32VoiceCapture>(device); }

std::vector<std::string> voiceCaptureDevices() {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));   // S_FALSE / RPC_E_CHANGED_MODE: already initialised
    std::vector<std::string> names;
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)))
        for (auto& e : captureEndpoints(en)) { names.push_back(e.first); release(e.second); }
    release(en);
    if (com) CoUninitialize();
    return names;
}

}  // namespace net
