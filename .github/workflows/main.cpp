#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <iomanip>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

static constexpr int ID_GPU = 1001, ID_INIT = 1002, ID_REFRESH = 1003;
static HWND g_window{}, g_gpu{}, g_status{}, g_init{};
static std::vector<ComPtr<IDXGIAdapter1>> g_adapters;
static ComPtr<ID3D11Device> g_device;
static ComPtr<ID3D11DeviceContext> g_context;
static ComPtr<ID3D11ComputeShader> g_upscaleShader;

static const char* kShader = R"(
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Target : register(u0);
cbuffer Params : register(b0) { uint outW; uint outH; float sharp; float pad; };
[numthreads(8,8,1)]
void CS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= outW || id.y >= outH) return;
    uint sw, sh; Source.GetDimensions(sw, sh);
    if (sw == 0 || sh == 0) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(outW, outH);
    float2 p = uv * float2(sw, sh) - 0.5;
    int2 ip = int2(floor(p)); float2 f = frac(p);
    int2 hi = int2(sw-1, sh-1), lo = int2(0,0);
    float4 a = Source.Load(int3(clamp(ip,lo,hi),0));
    float4 b = Source.Load(int3(clamp(ip+int2(1,0),lo,hi),0));
    float4 c = Source.Load(int3(clamp(ip+int2(0,1),lo,hi),0));
    float4 d = Source.Load(int3(clamp(ip+int2(1,1),lo,hi),0));
    float4 base = lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
    int2 q = int2(round(p));
    float3 l = Source.Load(int3(clamp(q+int2(-1,0),lo,hi),0)).rgb;
    float3 r = Source.Load(int3(clamp(q+int2(1,0),lo,hi),0)).rgb;
    float3 u = Source.Load(int3(clamp(q+int2(0,-1),lo,hi),0)).rgb;
    float3 dn= Source.Load(int3(clamp(q+int2(0,1),lo,hi),0)).rgb;
    float3 blur = (l+r+u+dn)*0.25;
    Target[id.xy] = float4(saturate(base.rgb + sharp*(base.rgb-blur)),base.a);
})";

static void SetStatus(const std::wstring& s) {
    if (g_status) SetWindowTextW(g_status, s.c_str());
}

static bool EnumerateAdapters() {
    g_adapters.clear();
    SendMessageW(g_gpu, CB_RESETCONTENT, 0, 0);
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { SetStatus(L"DXGI factory creation failed."); return false; }
    for (UINT i=0;;++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(a->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        g_adapters.push_back(a);
        SendMessageW(g_gpu, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(desc.Description));
    }
    if (g_adapters.empty()) {
        SendMessageW(g_gpu, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No hardware adapter found"));
        SendMessageW(g_gpu, CB_SETCURSEL, 0, 0);
        SetStatus(L"No hardware DXGI adapter found.");
        return false;
    }
    SendMessageW(g_gpu, CB_SETCURSEL, 0, 0);
    SetStatus(L"Ready. Select an adapter and initialize the GPU.");
    return true;
}

static bool InitializeGpu() {
    int index = static_cast<int>(SendMessageW(g_gpu, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(g_adapters.size())) {
        SetStatus(L"Select a valid hardware adapter first."); return false;
    }
    g_upscaleShader.Reset(); g_context.Reset(); g_device.Reset();
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = D3D11CreateDevice(g_adapters[index].Get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &g_device, &obtained, &g_context);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(g_adapters[index].Get(), D3D_DRIVER_TYPE_UNKNOWN,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels+1, 1,
            D3D11_SDK_VERSION, &g_device, &obtained, &g_context);
    }
    if (FAILED(hr)) {
        std::wstringstream ss; ss << L"D3D11 device creation failed (0x"
            << std::hex << std::uppercase << static_cast<unsigned long>(hr) << L").";
        SetStatus(ss.str()); return false;
    }
    ComPtr<ID3DBlob> bytecode, errors;
    hr = D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "CS", "cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
    if (FAILED(hr)) {
        std::wstring msg = L"HLSL compilation failed.";
        if (errors && errors->GetBufferPointer()) {
            int n = MultiByteToWideChar(CP_UTF8,0,
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<int>(errors->GetBufferSize()),nullptr,0);
            if (n > 0) { msg.resize(n); MultiByteToWideChar(CP_UTF8,0,
                static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<int>(errors->GetBufferSize()),msg.data(),n); }
        }
        SetStatus(msg); return false;
    }
    hr = g_device->CreateComputeShader(bytecode->GetBufferPointer(),
        bytecode->GetBufferSize(), nullptr, &g_upscaleShader);
    if (FAILED(hr)) { SetStatus(L"CreateComputeShader failed."); return false; }
    SetStatus(L"D3D11 device and HLSL compute shader initialized successfully. Capture is not implemented yet.");
    return true;
}

static void AddLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id=0) {
    HWND label=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,w,h,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g_window=hwnd;
        AddLabel(hwnd,L"RFPS",28,22,300,42);
        AddLabel(hwnd,L"REAL-TIME FRAME PROCESSING SYSTEM  /  PORTABLE BUILD",30,64,690,24);
        AddLabel(hwnd,L"GPU ADAPTER",30,120,220,22);
        g_gpu=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            30,148,610,260,hwnd,reinterpret_cast<HMENU>(ID_GPU),GetModuleHandleW(nullptr),nullptr);
        g_init=CreateWindowExW(0,L"BUTTON",L"Initialize GPU",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            30,202,180,38,hwnd,reinterpret_cast<HMENU>(ID_INIT),GetModuleHandleW(nullptr),nullptr);
        HWND refresh=CreateWindowExW(0,L"BUTTON",L"Refresh adapters",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            224,202,180,38,hwnd,reinterpret_cast<HMENU>(ID_REFRESH),GetModuleHandleW(nullptr),nullptr);
        AddLabel(hwnd,L"UPSCALE CORE",30,274,220,22);
        AddLabel(hwnd,L"D3D11 compute shader: bilinear resampling + adjustable unsharp mask",30,302,740,24);
        AddLabel(hwnd,L"FRAME GENERATION",30,350,240,22);
        AddLabel(hwnd,L"Unavailable: optical-flow engine is not implemented. No frame blending is used.",30,378,760,24);
        AddLabel(hwnd,L"CAPTURE",30,426,200,22);
        AddLabel(hwnd,L"Windows Graphics Capture backend is not implemented in this build.",30,454,760,24);
        g_status=CreateWindowExW(0,L"STATIC",L"Initializing adapter list...",WS_CHILD|WS_VISIBLE,
            30,520,800,48,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_gpu,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_init,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(refresh,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        EnumerateAdapters(); return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_INIT: InitializeGpu(); return 0;
        case ID_REFRESH: EnumerateAdapters(); return 0;
        } break;
    case WM_DESTROY:
        g_upscaleShader.Reset(); g_context.Reset(); g_device.Reset(); g_adapters.clear();
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t CLASS_NAME[] = L"RFPSPortableWindow";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=WndProc; wc.hInstance=instance; wc.lpszClassName=CLASS_NAME;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(RGB(16,20,29));
    wc.style=CS_HREDRAW|CS_VREDRAW;
    if (!RegisterClassExW(&wc)) return 1;
    HWND hwnd=CreateWindowExW(0,CLASS_NAME,L"RFPS — Portable Real-time Frame Processing System",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,880,640,nullptr,nullptr,instance,nullptr);
    if (!hwnd) return 2;
    ShowWindow(hwnd,show); UpdateWindow(hwnd);
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(wc.hbrBackground);
    return static_cast<int>(msg.wParam);
}
