/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#undef CreateWindow
#undef small
#undef min
#undef max

#include "DrawingEngineFactory.hpp"
#include "D3D11Shaders.h"

#include <SDL_syswm.h>
#include <SDL_video.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <openrct2/Diagnostic.h>
#include <openrct2/Game.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.Screen.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/PaletteType.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/Window.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Weather.h>

#ifndef DXGI_SWAP_EFFECT_FLIP_DISCARD
    #define DXGI_SWAP_EFFECT_FLIP_DISCARD static_cast<DXGI_SWAP_EFFECT>(4)
#endif
#ifndef DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
    #define DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL static_cast<DXGI_SWAP_EFFECT>(3)
#endif
#ifndef DXGI_PRESENT_ALLOW_TEARING
    #define DXGI_PRESENT_ALLOW_TEARING 0x00000200UL
#endif
#ifndef DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    #define DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING 2048
#endif
#ifndef D3D_FEATURE_LEVEL_11_1
    #define D3D_FEATURE_LEVEL_11_1 static_cast<D3D_FEATURE_LEVEL>(0xb100)
#endif
#ifndef D3D11_CREATE_DEVICE_BGRA_SUPPORT
    #define D3D11_CREATE_DEVICE_BGRA_SUPPORT 0x20
#endif

static constexpr IID kIID_IDXGIFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static constexpr IID kIID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static constexpr IID kIID_IDXGIFactory = { 0x7b716634, 0x20c7, 0x44ae, { 0xb2, 0xea, 0x53, 0x77, 0xb3, 0x29, 0x68, 0x88 } };

typedef HRESULT (WINAPI *PFN_D3D11_CREATE_DEVICE)(
    IDXGIAdapter*,
    D3D_DRIVER_TYPE,
    HMODULE,
    UINT,
    const D3D_FEATURE_LEVEL*,
    UINT,
    UINT,
    ID3D11Device**,
    D3D_FEATURE_LEVEL*,
    ID3D11DeviceContext**);

typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY1)(
    REFIID,
    void**);

template<typename T>
static void SafeRelease(T*& ptr)
{
    if (ptr != nullptr)
    {
        ptr->Release();
        ptr = nullptr;
    }
}

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Ui;

class D3D11DrawingEngine final : public X8DrawingEngine
{
private:
    constexpr static uint32_t kDirtyVisualTime = 40;
    constexpr static uint32_t kDirtyRegionAlpha = 100;

    IUiContext& _uiContext;
    SDL_Window* _window = nullptr;
    HWND _hwnd = nullptr;

    HMODULE _d3d11Module = nullptr;
    HMODULE _dxgiModule = nullptr;

    ID3D11Device* _device = nullptr;
    ID3D11DeviceContext* _context = nullptr;
    IDXGISwapChain* _swapChain = nullptr;
    ID3D11RenderTargetView* _renderTargetView = nullptr;

    ID3D11Texture2D* _screenTexture = nullptr;
    ID3D11ShaderResourceView* _screenSRV = nullptr;

    ID3D11Texture2D* _paletteTexture = nullptr;
    ID3D11ShaderResourceView* _paletteSRV = nullptr;

    ID3D11VertexShader* _vsQuad = nullptr;
    ID3D11PixelShader* _psPalette = nullptr;
    ID3D11PixelShader* _psPaletteSmooth = nullptr;
    ID3D11PixelShader* _psPaletteVibrant = nullptr;
    ID3D11PixelShader* _psPaletteCRT = nullptr;

    ID3D11VertexShader* _vsColor = nullptr;
    ID3D11PixelShader* _psColor = nullptr;
    ID3D11InputLayout* _colorInputLayout = nullptr;
    ID3D11Buffer* _colorVB = nullptr;

    ID3D11SamplerState* _pointSampler = nullptr;
    ID3D11SamplerState* _linearSampler = nullptr;
    ID3D11Buffer* _screenParamsBuffer = nullptr;

    ID3D11RasterizerState* _rasterizerState = nullptr;
    ID3D11BlendState* _blendStateOpaque = nullptr;
    ID3D11BlendState* _blendStateAlpha = nullptr;

    bool _useVsync = true;
    bool _allowTearing = false;
    uint32_t _backBufferWidth = 0;
    uint32_t _backBufferHeight = 0;

    uint32_t _paletteHWMapped[256] = { 0 };
    GamePalette _lastPalette = {};
    bool _hasPalette = false;

    struct ScreenParamsBuffer
    {
        float vScreenSize[4]; // width, height, 1/width, 1/height
    };

    struct ColorVertex
    {
        float x, y, z, w;
        float r, g, b, a;
    };

    std::vector<uint32_t> _dirtyVisualsTime;

public:
    explicit D3D11DrawingEngine(IUiContext& uiContext)
        : X8DrawingEngine(uiContext)
        , _uiContext(uiContext)
    {
        _window = static_cast<SDL_Window*>(_uiContext.GetWindow());
        _useVsync = Config::Get().general.useVSync;
    }

    ~D3D11DrawingEngine() override
    {
        SafeRelease(_renderTargetView);
        SafeRelease(_screenSRV);
        SafeRelease(_screenTexture);
        SafeRelease(_paletteSRV);
        SafeRelease(_paletteTexture);

        SafeRelease(_vsQuad);
        SafeRelease(_psPalette);
        SafeRelease(_psPaletteSmooth);
        SafeRelease(_psPaletteVibrant);
        SafeRelease(_psPaletteCRT);

        SafeRelease(_vsColor);
        SafeRelease(_psColor);
        SafeRelease(_colorInputLayout);
        SafeRelease(_colorVB);

        SafeRelease(_pointSampler);
        SafeRelease(_linearSampler);
        SafeRelease(_screenParamsBuffer);

        SafeRelease(_rasterizerState);
        SafeRelease(_blendStateOpaque);
        SafeRelease(_blendStateAlpha);

        SafeRelease(_swapChain);
        SafeRelease(_context);
        SafeRelease(_device);

        if (_d3d11Module != nullptr)
        {
            FreeLibrary(_d3d11Module);
            _d3d11Module = nullptr;
        }
        if (_dxgiModule != nullptr)
        {
            FreeLibrary(_dxgiModule);
            _dxgiModule = nullptr;
        }
    }

    void Initialise() override
    {
        if (_window == nullptr)
        {
            throw std::runtime_error("Direct3D 11: SDL window is null");
        }

        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(_window, &wmInfo) != SDL_TRUE || wmInfo.info.win.window == nullptr)
        {
            throw std::runtime_error("Direct3D 11: Unable to get Win32 HWND from SDL window");
        }
        _hwnd = wmInfo.info.win.window;

        _d3d11Module = LoadLibraryA("d3d11.dll");
        if (_d3d11Module == nullptr)
        {
            throw std::runtime_error("Direct3D 11: d3d11.dll is not available on this platform");
        }

        _dxgiModule = LoadLibraryA("dxgi.dll");
        if (_dxgiModule == nullptr)
        {
            throw std::runtime_error("Direct3D 11: dxgi.dll is not available on this platform");
        }

        auto pfnD3D11CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
            reinterpret_cast<void*>(GetProcAddress(_d3d11Module, "D3D11CreateDevice")));
        auto pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CREATE_DXGI_FACTORY1>(
            reinterpret_cast<void*>(GetProcAddress(_dxgiModule, "CreateDXGIFactory1")));

        if (pfnD3D11CreateDevice == nullptr || pfnCreateDXGIFactory1 == nullptr)
        {
            throw std::runtime_error("Direct3D 11: Missing required API entry points");
        }

        IDXGIFactory1* dxgiFactory1 = nullptr;
        HRESULT hr = pfnCreateDXGIFactory1(kIID_IDXGIFactory1, reinterpret_cast<void**>(&dxgiFactory1));
        if (FAILED(hr) || dxgiFactory1 == nullptr)
        {
            throw std::runtime_error("Direct3D 11: Failed to create DXGI factory");
        }

        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL obtainedLevel;

        hr = pfnD3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &_device,
            &obtainedLevel,
            &_context);

        if (FAILED(hr))
        {
            // Retry without 11.1 if unavailable
            hr = pfnD3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                createFlags,
                &featureLevels[1],
                static_cast<UINT>(std::size(featureLevels) - 1),
                D3D11_SDK_VERSION,
                &_device,
                &obtainedLevel,
                &_context);
        }

        if (FAILED(hr) || _device == nullptr || _context == nullptr)
        {
            dxgiFactory1->Release();
            throw std::runtime_error("Direct3D 11: Failed to create D3D11 hardware device");
        }

        int winW = 0, winH = 0;
        SDL_GetWindowSize(_window, &winW, &winH);
        if (winW <= 0)
            winW = 1280;
        if (winH <= 0)
            winH = 720;

        _backBufferWidth = static_cast<uint32_t>(winW);
        _backBufferHeight = static_cast<uint32_t>(winH);

        // First attempt modern DXGI Flip-Discard swap chain with tearing support (DXGI_SWAP_EFFECT_FLIP_DISCARD)
        // FLIP_DISCARD requires at least 3 buffers (triple-buffering) for smooth tear-free / uncapped presentation.
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 3;
        sd.BufferDesc.Width = static_cast<UINT>(winW);
        sd.BufferDesc.Height = static_cast<UINT>(winH);
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 0;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = _hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        hr = dxgiFactory1->CreateSwapChain(_device, &sd, &_swapChain);
        if (SUCCEEDED(hr) && _swapChain != nullptr)
        {
            _allowTearing = true;
            LOG_INFO("Direct3D 11: Created FLIP_DISCARD swap chain with ALLOW_TEARING (uncapped)");
        }
        else
        {
            // If FLIP_DISCARD with tearing is not supported (e.g. AMD driver in windowed mode),
            // do NOT use FLIP_DISCARD without tearing, as DWM forcibly locks it to the monitor refresh rate!
            // Fall back directly to DXGI_SWAP_EFFECT_DISCARD (blit model), which DWM NEVER caps to refresh rate.
            LOG_VERBOSE("Direct3D 11: FLIP_DISCARD with tearing unavailable, falling back to DISCARD bitblt model for uncapped FPS");
            sd.BufferCount = 1;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.Flags = 0;
            _allowTearing = false;
            hr = dxgiFactory1->CreateSwapChain(_device, &sd, &_swapChain);
            if (SUCCEEDED(hr) && _swapChain != nullptr)
            {
                LOG_INFO("Direct3D 11: Created DISCARD blit swap chain (uncapped)");
            }
        }

        if (FAILED(hr) || _swapChain == nullptr)
        {
            dxgiFactory1->Release();
            throw std::runtime_error("Direct3D 11: Failed to create swap chain");
        }

        IDXGIFactory* parentFactory = nullptr;
        if (SUCCEEDED(_swapChain->GetParent(kIID_IDXGIFactory, reinterpret_cast<void**>(&parentFactory))) && parentFactory != nullptr)
        {
            parentFactory->MakeWindowAssociation(_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
            parentFactory->Release();
        }

        dxgiFactory1->Release();

        // Create backbuffer render target view
        ID3D11Texture2D* backBuffer = nullptr;
        hr = _swapChain->GetBuffer(0, kIID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer));
        if (SUCCEEDED(hr) && backBuffer != nullptr)
        {
            _device->CreateRenderTargetView(backBuffer, nullptr, &_renderTargetView);
            backBuffer->Release();
        }

        // Create shaders
        _device->CreateVertexShader(g_vs_quad, sizeof(g_vs_quad), nullptr, &_vsQuad);
        _device->CreatePixelShader(g_ps_palette, sizeof(g_ps_palette), nullptr, &_psPalette);
        _device->CreatePixelShader(g_ps_palette_smooth, sizeof(g_ps_palette_smooth), nullptr, &_psPaletteSmooth);
        _device->CreatePixelShader(g_ps_palette_vibrant, sizeof(g_ps_palette_vibrant), nullptr, &_psPaletteVibrant);
        _device->CreatePixelShader(g_ps_palette_crt, sizeof(g_ps_palette_crt), nullptr, &_psPaletteCRT);

        _device->CreateVertexShader(g_vs_color, sizeof(g_vs_color), nullptr, &_vsColor);
        _device->CreatePixelShader(g_ps_color, sizeof(g_ps_color), nullptr, &_psColor);

        // Input layout for color debug quads
        const D3D11_INPUT_ELEMENT_DESC colorLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        _device->CreateInputLayout(colorLayout, 2, g_vs_color, sizeof(g_vs_color), &_colorInputLayout);

        // Dynamic vertex buffer for dirty visuals (up to 1024 vertices)
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(ColorVertex) * 1024;
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        _device->CreateBuffer(&vbDesc, nullptr, &_colorVB);

        // Point sampler (slot 0)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        _device->CreateSamplerState(&sampDesc, &_pointSampler);

        // Linear sampler (slot 1)
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        _device->CreateSamplerState(&sampDesc, &_linearSampler);

        // Screen params constant buffer
        D3D11_BUFFER_DESC bDesc = {};
        bDesc.ByteWidth = sizeof(ScreenParamsBuffer);
        bDesc.Usage = D3D11_USAGE_DYNAMIC;
        bDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        _device->CreateBuffer(&bDesc, nullptr, &_screenParamsBuffer);

        // Rasterizer state
        D3D11_RASTERIZER_DESC rDesc = {};
        rDesc.FillMode = D3D11_FILL_SOLID;
        rDesc.CullMode = D3D11_CULL_NONE;
        rDesc.ScissorEnable = FALSE;
        rDesc.DepthClipEnable = FALSE;
        _device->CreateRasterizerState(&rDesc, &_rasterizerState);

        // Blend states
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        _device->CreateBlendState(&blendDesc, &_blendStateOpaque);

        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        _device->CreateBlendState(&blendDesc, &_blendStateAlpha);

        // Palette texture (256 x 1)
        D3D11_TEXTURE2D_DESC pDesc = {};
        pDesc.Width = 256;
        pDesc.Height = 1;
        pDesc.MipLevels = 1;
        pDesc.ArraySize = 1;
        pDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        pDesc.SampleDesc.Count = 1;
        pDesc.Usage = D3D11_USAGE_DYNAMIC;
        pDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        pDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        _device->CreateTexture2D(&pDesc, nullptr, &_paletteTexture);
        if (_paletteTexture != nullptr)
        {
            _device->CreateShaderResourceView(_paletteTexture, nullptr, &_paletteSRV);
        }

        LOG_INFO("Direct3D 11 hardware drawing engine initialised successfully (%ux%u)", _backBufferWidth, _backBufferHeight);
    }

    void SetVSync(bool vsync) override
    {
        _useVsync = vsync;
    }

    void Resize(uint32_t width, uint32_t height) override
    {
        X8DrawingEngine::Resize(width, height);
        CreateScreenTexture(width, height);
    }

    void SetPalette(const GamePalette& palette) override
    {
        _lastPalette = palette;

        if (_device == nullptr || _context == nullptr || _paletteTexture == nullptr)
        {
            return;
        }

        bool paletteChanged = false;
        for (size_t i = 0; i < 256; i++)
        {
            const auto& src = palette[i];
            uint32_t mappedColor = 0xFF000000u
                | (static_cast<uint32_t>(src.red) << 16)
                | (static_cast<uint32_t>(src.green) << 8)
                | static_cast<uint32_t>(src.blue);
            if (_paletteHWMapped[i] != mappedColor)
            {
                _paletteHWMapped[i] = mappedColor;
                paletteChanged = true;
            }
        }

        if (paletteChanged || !_hasPalette)
        {
            _hasPalette = true;
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = _context->Map(_paletteTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (SUCCEEDED(hr) && mapped.pData != nullptr)
            {
                std::memcpy(mapped.pData, _paletteHWMapped, 256 * sizeof(uint32_t));
                _context->Unmap(_paletteTexture, 0);
            }
        }
    }

    void BeginDraw() override
    {
        X8DrawingEngine::BeginDraw();
    }

    void EndDraw() override
    {
        X8DrawingEngine::EndDraw();
        Display();
    }

    void PaintWindows() override
    {
        if (Weather::hasWeatherEffect() || gPaintForceRedraw)
        {
            WindowUpdateAllViewports();
            WindowDrawAll(_mainRT, 0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height));
        }
        else
        {
            X8DrawingEngine::PaintWindows();
        }
    }

protected:
    void OnDrawDirtyBlock(int32_t left, int32_t top, int32_t right, int32_t bottom) override
    {
        if (gShowDirtyVisuals)
        {
            const auto columns = ((right - left) + (_invalidationGrid.getBlockWidth() - 1)) / _invalidationGrid.getBlockWidth();
            const auto rows = ((bottom - top) + (_invalidationGrid.getBlockHeight() - 1)) / _invalidationGrid.getBlockHeight();
            const auto firstRow = top / _invalidationGrid.getBlockHeight();
            const auto firstColumn = left / _invalidationGrid.getBlockWidth();

            for (uint32_t y = 0; y < rows; y++)
            {
                for (uint32_t x = 0; x < columns; x++)
                {
                    SetDirtyVisualTime(firstColumn + x, firstRow + y, gCurrentRealTimeTicks + kDirtyVisualTime);
                }
            }
        }
    }

private:
    bool CreateScreenTexture(uint32_t width, uint32_t height)
    {
        if (_device == nullptr || width == 0 || height == 0)
        {
            return false;
        }

        SafeRelease(_screenSRV);
        SafeRelease(_screenTexture);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = _device->CreateTexture2D(&desc, nullptr, &_screenTexture);
        if (SUCCEEDED(hr) && _screenTexture != nullptr)
        {
            hr = _device->CreateShaderResourceView(_screenTexture, nullptr, &_screenSRV);
            return SUCCEEDED(hr);
        }
        return false;
    }

    void ResizeSwapChain(int windowWidth, int windowHeight)
    {
        if (_swapChain == nullptr || _device == nullptr || windowWidth <= 0 || windowHeight <= 0)
        {
            return;
        }

        SafeRelease(_renderTargetView);

        UINT swapChainFlags = _allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = _swapChain->ResizeBuffers(
            0,
            static_cast<UINT>(windowWidth),
            static_cast<UINT>(windowHeight),
            DXGI_FORMAT_UNKNOWN,
            swapChainFlags);

        if (FAILED(hr))
        {
            LOG_WARNING("Direct3D 11: ResizeBuffers failed (0x%08lX)", static_cast<unsigned long>(hr));
            return;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        hr = _swapChain->GetBuffer(0, kIID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer));
        if (SUCCEEDED(hr) && backBuffer != nullptr)
        {
            _device->CreateRenderTargetView(backBuffer, nullptr, &_renderTargetView);
            backBuffer->Release();
        }

        _backBufferWidth = static_cast<uint32_t>(windowWidth);
        _backBufferHeight = static_cast<uint32_t>(windowHeight);
    }

    void Display()
    {
        if (_device == nullptr || _context == nullptr || _swapChain == nullptr || _screenTexture == nullptr)
        {
            return;
        }

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(_window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0)
        {
            return;
        }

        if (static_cast<uint32_t>(windowWidth) != _backBufferWidth
            || static_cast<uint32_t>(windowHeight) != _backBufferHeight)
        {
            ResizeSwapChain(windowWidth, windowHeight);
        }

        if (_renderTargetView == nullptr)
        {
            return;
        }

        // Map and copy 8-bit palette indices to GPU texture
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = _context->Map(_screenTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr) && mapped.pData != nullptr)
        {
            if (mapped.RowPitch == _width)
            {
                std::memcpy(mapped.pData, _bits, _width * _height);
            }
            else
            {
                uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
                const uint8_t* src = reinterpret_cast<const uint8_t*>(_bits);
                for (uint32_t y = 0; y < _height; y++)
                {
                    std::memcpy(dst, src, _width);
                    dst += mapped.RowPitch;
                    src += _width;
                }
            }
            _context->Unmap(_screenTexture, 0);
        }

        // Update screen parameters constant buffer
        if (_screenParamsBuffer != nullptr)
        {
            D3D11_MAPPED_SUBRESOURCE cbMapped;
            if (SUCCEEDED(_context->Map(_screenParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped)))
            {
                auto* p = static_cast<ScreenParamsBuffer*>(cbMapped.pData);
                p->vScreenSize[0] = static_cast<float>(_width);
                p->vScreenSize[1] = static_cast<float>(_height);
                p->vScreenSize[2] = 1.0f / static_cast<float>(_width);
                p->vScreenSize[3] = 1.0f / static_cast<float>(_height);
                _context->Unmap(_screenParamsBuffer, 0);
            }
        }

        // Configure pipeline
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(windowWidth);
        vp.Height = static_cast<float>(windowHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        _context->RSSetViewports(1, &vp);
        _context->RSSetState(_rasterizerState);

        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        _context->OMSetBlendState(_blendStateOpaque, blendFactor, 0xFFFFFFFF);
        _context->OMSetRenderTargets(1, &_renderTargetView, nullptr);

        _context->IASetInputLayout(nullptr);
        _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        _context->VSSetShader(_vsQuad, nullptr, 0);

        // Shader selection
        ID3D11PixelShader* selectedPS = _psPalette;
        ScaleQuality scaleQuality = _uiContext.GetScaleQuality();

        if ((scaleQuality == ScaleQuality::linear || scaleQuality == ScaleQuality::smoothNearestNeighbour)
            && _psPaletteSmooth != nullptr)
        {
            selectedPS = _psPaletteSmooth;
        }

        _context->PSSetShader(selectedPS, nullptr, 0);

        ID3D11ShaderResourceView* srvs[2] = { _screenSRV, _paletteSRV };
        _context->PSSetShaderResources(0, 2, srvs);

        ID3D11SamplerState* samplers[2] = { _pointSampler, _linearSampler };
        _context->PSSetSamplers(0, 2, samplers);

        if (_screenParamsBuffer != nullptr)
        {
            _context->PSSetConstantBuffers(0, 1, &_screenParamsBuffer);
        }

        // Draw full-screen triangle
        _context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        _context->PSSetShaderResources(0, 2, nullSRVs);

        DrawDirtyVisuals();

        // Present
        UINT syncInterval = _useVsync ? 1 : 0;
        UINT presentFlags = (!_useVsync && _allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        hr = _swapChain->Present(syncInterval, presentFlags);
        if (FAILED(hr) && presentFlags != 0)
        {
            _swapChain->Present(syncInterval, 0);
        }
    }

    uint32_t GetDirtyVisualTime(uint32_t x, uint32_t y)
    {
        uint32_t index = y * _invalidationGrid.getColumnCount() + x;
        if (index >= _dirtyVisualsTime.size())
            return 0;
        return _dirtyVisualsTime[index];
    }

    void SetDirtyVisualTime(uint32_t x, uint32_t y, uint32_t ticks)
    {
        uint32_t index = y * _invalidationGrid.getColumnCount() + x;
        if (index >= _dirtyVisualsTime.size())
        {
            _dirtyVisualsTime.resize(_invalidationGrid.getColumnCount() * _invalidationGrid.getRowCount(), 0);
        }
        _dirtyVisualsTime[index] = ticks;
    }

    void DrawDirtyVisuals()
    {
        if (!gShowDirtyVisuals || _context == nullptr || _device == nullptr || _colorVB == nullptr || _colorInputLayout == nullptr)
        {
            return;
        }

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(_window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0 || _width == 0 || _height == 0)
        {
            return;
        }

        float scaleX = static_cast<float>(windowWidth) / static_cast<float>(_width);
        float scaleY = static_cast<float>(windowHeight) / static_cast<float>(_height);

        std::vector<ColorVertex> vertices;
        for (uint32_t y = 0; y < _invalidationGrid.getRowCount(); y++)
        {
            for (uint32_t x = 0; x < _invalidationGrid.getColumnCount(); x++)
            {
                uint32_t expireTime = GetDirtyVisualTime(x, y);
                int32_t timeLeft = static_cast<int32_t>(expireTime - gCurrentRealTimeTicks);
                if (timeLeft > 0)
                {
                    float alpha = (static_cast<float>(timeLeft) * (static_cast<float>(kDirtyRegionAlpha) / 255.0f)) / static_cast<float>(kDirtyVisualTime);
                    float rx1 = (x * _invalidationGrid.getBlockWidth() * scaleX) / static_cast<float>(windowWidth) * 2.0f - 1.0f;
                    float ry1 = 1.0f - (y * _invalidationGrid.getBlockHeight() * scaleY) / static_cast<float>(windowHeight) * 2.0f;
                    float rx2 = ((x + 1) * _invalidationGrid.getBlockWidth() * scaleX) / static_cast<float>(windowWidth) * 2.0f - 1.0f;
                    float ry2 = 1.0f - ((y + 1) * _invalidationGrid.getBlockHeight() * scaleY) / static_cast<float>(windowHeight) * 2.0f;

                    ColorVertex c0 = { rx1, ry1, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha };
                    ColorVertex c1 = { rx2, ry1, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha };
                    ColorVertex c2 = { rx1, ry2, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha };
                    ColorVertex c3 = { rx2, ry2, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha };

                    vertices.push_back(c0);
                    vertices.push_back(c1);
                    vertices.push_back(c2);
                    vertices.push_back(c2);
                    vertices.push_back(c1);
                    vertices.push_back(c3);
                }
            }
        }

        if (!vertices.empty())
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(_context->Map(_colorVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                size_t maxVertices = 1024;
                size_t count = std::min(vertices.size(), maxVertices);
                std::memcpy(mapped.pData, vertices.data(), count * sizeof(ColorVertex));
                _context->Unmap(_colorVB, 0);

                float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                _context->OMSetBlendState(_blendStateAlpha, blendFactor, 0xFFFFFFFF);

                UINT stride = sizeof(ColorVertex);
                UINT offset = 0;
                _context->IASetInputLayout(_colorInputLayout);
                _context->IASetVertexBuffers(0, 1, &_colorVB, &stride, &offset);
                _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                _context->VSSetShader(_vsColor, nullptr, 0);
                _context->PSSetShader(_psColor, nullptr, 0);

                _context->Draw(static_cast<UINT>(count), 0);
            }
        }
    }
};

std::unique_ptr<IDrawingEngine> Ui::CreateD3D11DrawingEngine(IUiContext& uiContext)
{
    return std::make_unique<D3D11DrawingEngine>(uiContext);
}

#endif // _WIN32
