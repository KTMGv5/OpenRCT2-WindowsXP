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
#include <windows.h>
#undef CreateWindow
#undef small

#include "DrawingEngineFactory.hpp"
#include "D3D9Shaders.h"

#include <SDL_syswm.h>
#include <SDL_video.h>
#include <d3d9.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <openrct2/Diagnostic.h>
#include <openrct2/Game.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.Screen.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/Window.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ui/UiContext.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Ui;

class D3D9DrawingEngine final : public X8DrawingEngine
{
private:
    constexpr static uint32_t kDirtyVisualTime = 40;
    constexpr static uint32_t kDirtyRegionAlpha = 100;

    IUiContext& _uiContext;
    SDL_Window* _window = nullptr;
    HWND _hwnd = nullptr;
    HMODULE _d3d9Module = nullptr;
    IDirect3D9* _d3d = nullptr;
    IDirect3DDevice9* _device = nullptr;
    IDirect3DTexture9* _screenTexture = nullptr;
    IDirect3DTexture9* _paletteTexture = nullptr;
    IDirect3DPixelShader9* _pixelShader = nullptr;
    IDirect3DPixelShader9* _pixelShaderSmooth = nullptr;
    IDirect3DPixelShader9* _pixelShaderCRT = nullptr;
    IDirect3DPixelShader9* _pixelShaderVibrant = nullptr;
    D3DFORMAT _screenTextureFormat = D3DFMT_UNKNOWN;
    bool _usePixelShader = false;
    D3DPRESENT_PARAMETERS _d3dpp = {};
    bool _isDynamicTexture = false;
    bool _useVsync = true;
    uint32_t _backBufferWidth = 0;
    uint32_t _backBufferHeight = 0;

    uint32_t _paletteHWMapped[256] = { 0 };
    uint32_t _lightPaletteHWMapped[256] = { 0 };
    GamePalette _lastPalette = {};
    bool _hasPalette = false;

    std::vector<uint32_t> _dirtyVisualsTime;

    struct D3DVertex
    {
        float x, y, z, rhw;
        float u, v;
    };
    constexpr static DWORD kD3DFVF = D3DFVF_XYZRHW | D3DFVF_TEX1;

    struct ColorVertex
    {
        float x, y, z, rhw;
        DWORD color;
    };
    constexpr static DWORD kColorFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

public:
    explicit D3D9DrawingEngine(IUiContext& uiContext)
        : X8DrawingEngine(uiContext)
        , _uiContext(uiContext)
    {
        _window = static_cast<SDL_Window*>(_uiContext.GetWindow());
    }

    ~D3D9DrawingEngine() override
    {
        if (_pixelShader != nullptr)
        {
            _pixelShader->Release();
            _pixelShader = nullptr;
        }
        if (_pixelShaderSmooth != nullptr)
        {
            _pixelShaderSmooth->Release();
            _pixelShaderSmooth = nullptr;
        }
        if (_pixelShaderCRT != nullptr)
        {
            _pixelShaderCRT->Release();
            _pixelShaderCRT = nullptr;
        }
        if (_pixelShaderVibrant != nullptr)
        {
            _pixelShaderVibrant->Release();
            _pixelShaderVibrant = nullptr;
        }
        if (_paletteTexture != nullptr)
        {
            _paletteTexture->Release();
            _paletteTexture = nullptr;
        }
        if (_screenTexture != nullptr)
        {
            _screenTexture->Release();
            _screenTexture = nullptr;
        }
        if (_device != nullptr)
        {
            _device->Release();
            _device = nullptr;
        }
        if (_d3d != nullptr)
        {
            _d3d->Release();
            _d3d = nullptr;
        }
        if (_d3d9Module != nullptr)
        {
            FreeLibrary(_d3d9Module);
            _d3d9Module = nullptr;
        }
    }

    void Initialise() override
    {
        if (_window == nullptr)
        {
            throw std::runtime_error("Direct3D 9: SDL window is null");
        }

        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(_window, &wmInfo) != SDL_TRUE)
        {
            throw std::runtime_error("Direct3D 9: Unable to get Win32 HWND from SDL window");
        }
        _hwnd = wmInfo.info.win.window;
        if (_hwnd == nullptr)
        {
            throw std::runtime_error("Direct3D 9: Win32 HWND is null");
        }

        _d3d9Module = LoadLibraryA("d3d9.dll");
        if (_d3d9Module == nullptr)
        {
            throw std::runtime_error("Direct3D 9: d3d9.dll not found on system");
        }

        using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
        auto pfnDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(reinterpret_cast<void*>(GetProcAddress(_d3d9Module, "Direct3DCreate9")));
        if (pfnDirect3DCreate9 == nullptr)
        {
            throw std::runtime_error("Direct3D 9: Direct3DCreate9 export not found in d3d9.dll");
        }

        _d3d = pfnDirect3DCreate9(D3D_SDK_VERSION);
        if (_d3d == nullptr)
        {
            throw std::runtime_error("Direct3D 9: Direct3DCreate9 returned null");
        }

        int initWidth = 0, initHeight = 0;
        SDL_GetWindowSize(_window, &initWidth, &initHeight);
        if (initWidth <= 0 || initHeight <= 0)
        {
            RECT rcClient = {};
            if (GetClientRect(_hwnd, &rcClient))
            {
                initWidth = rcClient.right - rcClient.left;
                initHeight = rcClient.bottom - rcClient.top;
            }
        }
        if (initWidth <= 0)
            initWidth = 1280;
        if (initHeight <= 0)
            initHeight = 720;

        _d3dpp = {};
        _d3dpp.Windowed = TRUE;
        _d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        _d3dpp.hDeviceWindow = _hwnd;
        _d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
        _d3dpp.BackBufferCount = 1;
        _d3dpp.BackBufferWidth = static_cast<UINT>(initWidth);
        _d3dpp.BackBufferHeight = static_cast<UINT>(initHeight);
        _d3dpp.EnableAutoDepthStencil = FALSE;
        _d3dpp.PresentationInterval = _useVsync ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_IMMEDIATE;

        DWORD behaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
        HRESULT hr = _d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            _hwnd,
            behaviorFlags,
            &_d3dpp,
            &_device);

        if (FAILED(hr))
        {
            LOG_VERBOSE("Direct3D 9: Hardware vertex processing not available, trying software vertex processing...");
            behaviorFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
            hr = _d3d->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                _hwnd,
                behaviorFlags,
                &_d3dpp,
                &_device);
        }

        if (FAILED(hr) || _device == nullptr)
        {
            throw std::runtime_error("Direct3D 9: Failed to create Direct3D 9 HAL device");
        }

        _backBufferWidth = static_cast<uint32_t>(initWidth);
        _backBufferHeight = static_cast<uint32_t>(initHeight);

        D3DVIEWPORT9 vp = {};
        vp.X = 0;
        vp.Y = 0;
        vp.Width = static_cast<DWORD>(initWidth);
        vp.Height = static_cast<DWORD>(initHeight);
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        _device->SetViewport(&vp);

        // Initialize HLSL Pixel Shader palette pipeline if L8 texture format is supported
        bool l8Supported = SUCCEEDED(_d3d->CheckDeviceFormat(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            _d3dpp.BackBufferFormat,
            D3DUSAGE_DYNAMIC,
            D3DRTYPE_TEXTURE,
            D3DFMT_L8));

        if (!l8Supported)
        {
            l8Supported = SUCCEEDED(_d3d->CheckDeviceFormat(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                _d3dpp.BackBufferFormat,
                0,
                D3DRTYPE_TEXTURE,
                D3DFMT_L8));
        }

        if (l8Supported)
        {
            HRESULT hrPS = _device->CreatePixelShader(
                reinterpret_cast<const DWORD*>(g_ps_palette), &_pixelShader);
            _device->CreatePixelShader(
                reinterpret_cast<const DWORD*>(g_ps_palette_smooth), &_pixelShaderSmooth);
            _device->CreatePixelShader(
                reinterpret_cast<const DWORD*>(g_ps_palette_crt), &_pixelShaderCRT);
            _device->CreatePixelShader(
                reinterpret_cast<const DWORD*>(g_ps_palette_vibrant), &_pixelShaderVibrant);

            if (SUCCEEDED(hrPS))
            {
                HRESULT hrPal = _device->CreateTexture(
                    256, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &_paletteTexture, nullptr);

                if (SUCCEEDED(hrPal) && _paletteTexture != nullptr)
                {
                    _usePixelShader = true;
                    LOG_INFO("Direct3D 9: Hardware Pixel Shader palette conversion active (SM 2.0)");
                }
            }
        }

        if (!_usePixelShader)
        {
            LOG_INFO("Direct3D 9: Using software palette conversion with hardware blit");
        }

        LOG_INFO("Direct3D 9 hardware drawing engine initialised successfully (%ux%u)", _backBufferWidth, _backBufferHeight);
    }

    void SetVSync(bool vsync) override
    {
        if (_useVsync != vsync)
        {
            _useVsync = vsync;
            if (_device != nullptr)
            {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(_window, &winW, &winH);
                if (winW <= 0 || winH <= 0)
                {
                    winW = static_cast<int>(_backBufferWidth);
                    winH = static_cast<int>(_backBufferHeight);
                }
                if (winW > 0 && winH > 0)
                {
                    ResetDevice(winW, winH, _width, _height);
                }
            }
        }
    }

    void Resize(uint32_t width, uint32_t height) override
    {
        if (width == 0 || height == 0 || _device == nullptr)
        {
            return;
        }

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(_window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0)
        {
            RECT rcClient = {};
            if (_hwnd != nullptr && GetClientRect(_hwnd, &rcClient))
            {
                windowWidth = rcClient.right - rcClient.left;
                windowHeight = rcClient.bottom - rcClient.top;
            }
        }

        bool resetDone = false;
        if (windowWidth > 0 && windowHeight > 0 && (_hwnd == nullptr || !IsIconic(_hwnd)))
        {
            if (static_cast<uint32_t>(windowWidth) != _backBufferWidth ||
                static_cast<uint32_t>(windowHeight) != _backBufferHeight)
            {
                resetDone = ResetDevice(windowWidth, windowHeight, width, height);
            }
        }

        if (!resetDone)
        {
            CreateScreenTexture(width, height);
        }

        X8DrawingEngine::Resize(width, height);
    }

    DrawingEngineFlags GetFlags() override
    {
        return { DrawingEngineFlag::dirtyOptimisations };
    }

    void SetPalette(const GamePalette& palette) override
    {
        _lastPalette = palette;
        _hasPalette = true;

        for (int32_t i = 0; i < 256; i++)
        {
            _paletteHWMapped[i] = 0xFF000000u
                | (static_cast<uint32_t>(palette[i].red) << 16)
                | (static_cast<uint32_t>(palette[i].green) << 8)
                | static_cast<uint32_t>(palette[i].blue);
        }

        if (_paletteTexture != nullptr)
        {
            D3DLOCKED_RECT lr = {};
            if (SUCCEEDED(_paletteTexture->LockRect(0, &lr, nullptr, 0)))
            {
                std::memcpy(lr.pBits, _paletteHWMapped, 256 * sizeof(uint32_t));
                _paletteTexture->UnlockRect(0);
            }
        }

        if (Config::Get().general.enableLightFx)
        {
            auto& lightPalette = LightFx::GetPalette();
            for (int32_t i = 0; i < 256; i++)
            {
                const auto& src = lightPalette[i];
                _lightPaletteHWMapped[i] = (static_cast<uint32_t>(src.alpha) << 24)
                    | (static_cast<uint32_t>(src.red) << 16)
                    | (static_cast<uint32_t>(src.green) << 8)
                    | static_cast<uint32_t>(src.blue);
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
    bool ResetDevice(int windowWidth, int windowHeight, uint32_t canvasWidth, uint32_t canvasHeight)
    {
        if (_device == nullptr || windowWidth <= 0 || windowHeight <= 0)
        {
            return false;
        }

        if (_hwnd != nullptr && IsIconic(_hwnd))
        {
            return false;
        }

        // Must release all D3DPOOL_DEFAULT resources before resetting the device
        if (_screenTexture != nullptr)
        {
            _screenTexture->Release();
            _screenTexture = nullptr;
        }

        _d3dpp.BackBufferWidth = static_cast<UINT>(windowWidth);
        _d3dpp.BackBufferHeight = static_cast<UINT>(windowHeight);
        _d3dpp.PresentationInterval = _useVsync ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_IMMEDIATE;

        HRESULT hr = _device->Reset(&_d3dpp);
        if (FAILED(hr))
        {
            LOG_WARNING("Direct3D 9: Device Reset failed (0x%08lX)", static_cast<unsigned long>(hr));
            return false;
        }

        _backBufferWidth = static_cast<uint32_t>(windowWidth);
        _backBufferHeight = static_cast<uint32_t>(windowHeight);

        D3DVIEWPORT9 vp = {};
        vp.X = 0;
        vp.Y = 0;
        vp.Width = static_cast<DWORD>(windowWidth);
        vp.Height = static_cast<DWORD>(windowHeight);
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        _device->SetViewport(&vp);

        if (canvasWidth > 0 && canvasHeight > 0)
        {
            CreateScreenTexture(canvasWidth, canvasHeight);
        }

        GfxInvalidateScreen();
        return true;
    }

    bool CreateScreenTexture(uint32_t width, uint32_t height)
    {
        if (_device == nullptr || width == 0 || height == 0)
        {
            return false;
        }

        if (_screenTexture != nullptr)
        {
            _screenTexture->Release();
            _screenTexture = nullptr;
        }

        D3DFORMAT targetFormat = (_usePixelShader && !Config::Get().general.enableLightFx)
            ? D3DFMT_L8
            : D3DFMT_X8R8G8B8;

        // Try dynamic texture in D3DPOOL_DEFAULT first
        HRESULT hr = _device->CreateTexture(
            width,
            height,
            1,
            D3DUSAGE_DYNAMIC,
            targetFormat,
            D3DPOOL_DEFAULT,
            &_screenTexture,
            nullptr);

        if (SUCCEEDED(hr) && _screenTexture != nullptr)
        {
            _isDynamicTexture = true;
            _screenTextureFormat = targetFormat;
        }
        else
        {
            // Fall back to managed texture
            hr = _device->CreateTexture(
                width,
                height,
                1,
                0,
                targetFormat,
                D3DPOOL_MANAGED,
                &_screenTexture,
                nullptr);

            if (SUCCEEDED(hr) && _screenTexture != nullptr)
            {
                _isDynamicTexture = false;
                _screenTextureFormat = targetFormat;
            }
            else if (targetFormat != D3DFMT_X8R8G8B8)
            {
                // Fall back to standard X8R8G8B8 format
                hr = _device->CreateTexture(
                    width,
                    height,
                    1,
                    D3DUSAGE_DYNAMIC,
                    D3DFMT_X8R8G8B8,
                    D3DPOOL_DEFAULT,
                    &_screenTexture,
                    nullptr);

                if (SUCCEEDED(hr) && _screenTexture != nullptr)
                {
                    _isDynamicTexture = true;
                    _screenTextureFormat = D3DFMT_X8R8G8B8;
                }
                else
                {
                    hr = _device->CreateTexture(
                        width,
                        height,
                        1,
                        0,
                        D3DFMT_X8R8G8B8,
                        D3DPOOL_MANAGED,
                        &_screenTexture,
                        nullptr);

                    if (SUCCEEDED(hr) && _screenTexture != nullptr)
                    {
                        _isDynamicTexture = false;
                        _screenTextureFormat = D3DFMT_X8R8G8B8;
                    }
                    else
                    {
                        LOG_WARNING("Direct3D 9: Failed to create screen texture (%ux%u): 0x%08lX", width, height, static_cast<unsigned long>(hr));
                        return false;
                    }
                }
            }
            else
            {
                LOG_WARNING("Direct3D 9: Failed to create screen texture (%ux%u): 0x%08lX", width, height, static_cast<unsigned long>(hr));
                return false;
            }
        }

        if (_hasPalette)
        {
            SetPalette(_lastPalette);
        }

        return true;
    }

    void Display()
    {
        if (_device == nullptr || _width == 0 || _height == 0)
        {
            return;
        }

        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(_window, &windowWidth, &windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0)
        {
            RECT rcClient = {};
            if (_hwnd != nullptr && GetClientRect(_hwnd, &rcClient))
            {
                windowWidth = rcClient.right - rcClient.left;
                windowHeight = rcClient.bottom - rcClient.top;
            }
        }

        if (windowWidth <= 0 || windowHeight <= 0 || (_hwnd != nullptr && IsIconic(_hwnd)))
        {
            return;
        }

        HRESULT hrCoop = _device->TestCooperativeLevel();
        if (hrCoop == D3DERR_DEVICELOST)
        {
            return;
        }
        else if (hrCoop == D3DERR_DEVICENOTRESET)
        {
            if (!ResetDevice(windowWidth, windowHeight, _width, _height))
            {
                return;
            }
        }
        else if (static_cast<uint32_t>(windowWidth) != _backBufferWidth ||
                 static_cast<uint32_t>(windowHeight) != _backBufferHeight)
        {
            GetContext()->GetUiContext().TriggerResize();
            if (static_cast<uint32_t>(windowWidth) != _backBufferWidth ||
                static_cast<uint32_t>(windowHeight) != _backBufferHeight)
            {
                if (!ResetDevice(windowWidth, windowHeight, _width, _height))
                {
                    return;
                }
            }
        }

        if (_screenTexture == nullptr)
        {
            if (!CreateScreenTexture(_width, _height))
            {
                return;
            }
        }

        if (_bits == nullptr)
        {
            return;
        }

        bool usingHardwarePalette = (_screenTextureFormat == D3DFMT_L8 && _paletteTexture != nullptr && _pixelShader != nullptr);

        D3DLOCKED_RECT lockedRect = {};
        DWORD lockFlags = _isDynamicTexture ? D3DLOCK_DISCARD : 0;
        if (SUCCEEDED(_screenTexture->LockRect(0, &lockedRect, nullptr, lockFlags)))
        {
            if (usingHardwarePalette)
            {
                if (lockedRect.Pitch == static_cast<int32_t>(_width))
                {
                    std::memcpy(lockedRect.pBits, _bits, _width * _height);
                }
                else
                {
                    const uint8_t* srcRow = reinterpret_cast<const uint8_t*>(_bits);
                    uint8_t* dstRow = static_cast<uint8_t*>(lockedRect.pBits);
                    for (uint32_t y = 0; y < _height; y++)
                    {
                        std::memcpy(dstRow, srcRow, _width);
                        dstRow += lockedRect.Pitch;
                        srcRow += _width;
                    }
                }
            }
            else
            {
                auto* viewport = WindowGetViewport(WindowGetMain());
                if (Config::Get().general.enableLightFx && viewport != nullptr)
                {
                    LightFx::RenderToTexture(
                        *viewport, lockedRect.pBits, lockedRect.Pitch, _bits, _width, _height,
                        _paletteHWMapped, _lightPaletteHWMapped);
                }
                else
                {
                    CopyBitsToTexture(
                        lockedRect.pBits, lockedRect.Pitch, _bits, static_cast<int32_t>(_width),
                        static_cast<int32_t>(_height), _paletteHWMapped);
                }
            }
            _screenTexture->UnlockRect(0);
        }

        float x1 = -0.5f;
        float y1 = -0.5f;
        float x2 = static_cast<float>(windowWidth) - 0.5f;
        float y2 = static_cast<float>(windowHeight) - 0.5f;

        D3DVertex vertices[4] = {
            { x1, y1, 0.0f, 1.0f, 0.0f, 0.0f },
            { x2, y1, 0.0f, 1.0f, 1.0f, 0.0f },
            { x1, y2, 0.0f, 1.0f, 0.0f, 1.0f },
            { x2, y2, 0.0f, 1.0f, 1.0f, 1.0f },
        };

        ScaleQuality scaleQuality = GetContext()->GetUiContext().GetScaleQuality();

        D3DVIEWPORT9 vp = {};
        vp.X = 0;
        vp.Y = 0;
        vp.Width = static_cast<DWORD>(windowWidth);
        vp.Height = static_cast<DWORD>(windowHeight);
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        _device->SetViewport(&vp);

        if (SUCCEEDED(_device->BeginScene()))
        {
            _device->SetFVF(kD3DFVF);
            _device->SetRenderState(D3DRS_LIGHTING, FALSE);
            _device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            _device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
            _device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

            if (usingHardwarePalette)
            {
                _device->SetTexture(0, _screenTexture);
                _device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                _device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                _device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                _device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

                _device->SetTexture(1, _paletteTexture);
                _device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                _device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                _device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                _device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

                int32_t shaderEffect = Config::Get().general.d3d9ShaderEffect;
                if (shaderEffect == 2 && _pixelShaderCRT != nullptr)
                {
                    float crtParams[4] = {
                        static_cast<float>(_width),
                        static_cast<float>(_height),
                        static_cast<float>(windowWidth),
                        static_cast<float>(windowHeight)
                    };
                    _device->SetPixelShaderConstantF(0, crtParams, 1);
                    _device->SetPixelShader(_pixelShaderCRT);
                }
                else if (shaderEffect == 3 && _pixelShaderVibrant != nullptr)
                {
                    _device->SetPixelShader(_pixelShaderVibrant);
                }
                else if (shaderEffect == 1)
                {
                    _device->SetPixelShader(_pixelShader);
                }
                else if ((scaleQuality == ScaleQuality::linear || scaleQuality == ScaleQuality::smoothNearestNeighbour)
                    && _pixelShaderSmooth != nullptr)
                {
                    float canvasParams[4] = {
                        static_cast<float>(_width),
                        static_cast<float>(_height),
                        1.0f / static_cast<float>(_width),
                        1.0f / static_cast<float>(_height)
                    };
                    _device->SetPixelShaderConstantF(0, canvasParams, 1);
                    _device->SetPixelShader(_pixelShaderSmooth);
                }
                else
                {
                    _device->SetPixelShader(_pixelShader);
                }
            }
            else
            {
                _device->SetTexture(0, _screenTexture);
                _device->SetTexture(1, nullptr);
                _device->SetPixelShader(nullptr);

                D3DTEXTUREFILTERTYPE filter = (scaleQuality == ScaleQuality::linear || scaleQuality == ScaleQuality::smoothNearestNeighbour)
                    ? D3DTEXF_LINEAR
                    : D3DTEXF_POINT;

                _device->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
                _device->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
                _device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                _device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            }

            _device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(D3DVertex));

            _device->SetPixelShader(nullptr);
            _device->SetTexture(1, nullptr);

            if (gShowDirtyVisuals)
            {
                RenderDirtyVisuals(windowWidth, windowHeight);
            }

            _device->EndScene();
        }

        _device->Present(nullptr, nullptr, nullptr, nullptr);
    }

    void CopyBitsToTexture(void* pixels, int32_t pitch, const PaletteIndex* src, int32_t width, int32_t height, const uint32_t* palette)
    {
        if (pitch == width * 4)
        {
            uint32_t* dst = static_cast<uint32_t*>(pixels);
            int32_t count = width * height;
            int32_t blocks = count / 4;
            int32_t remainder = count % 4;

            while (blocks-- > 0)
            {
                dst[0] = palette[EnumValue(src[0])];
                dst[1] = palette[EnumValue(src[1])];
                dst[2] = palette[EnumValue(src[2])];
                dst[3] = palette[EnumValue(src[3])];
                src += 4;
                dst += 4;
            }
            while (remainder-- > 0)
            {
                *dst++ = palette[EnumValue(*src++)];
            }
        }
        else
        {
            uint8_t* rowDst = static_cast<uint8_t*>(pixels);
            const PaletteIndex* rowSrc = src;
            for (int32_t y = 0; y < height; y++)
            {
                uint32_t* dst = reinterpret_cast<uint32_t*>(rowDst);
                const PaletteIndex* s = rowSrc;
                int32_t blocks = width / 4;
                int32_t remainder = width % 4;

                while (blocks-- > 0)
                {
                    dst[0] = palette[EnumValue(s[0])];
                    dst[1] = palette[EnumValue(s[1])];
                    dst[2] = palette[EnumValue(s[2])];
                    dst[3] = palette[EnumValue(s[3])];
                    s += 4;
                    dst += 4;
                }
                while (remainder-- > 0)
                {
                    *dst++ = palette[EnumValue(*s++)];
                }
                rowDst += pitch;
                rowSrc += width;
            }
        }
    }

    uint32_t GetDirtyVisualTime(uint32_t x, uint32_t y)
    {
        uint32_t result = 0;
        uint32_t i = y * _invalidationGrid.getColumnCount() + x;
        if (_dirtyVisualsTime.size() > i)
        {
            result = _dirtyVisualsTime[i];
        }
        return result;
    }

    void SetDirtyVisualTime(uint32_t x, uint32_t y, uint32_t value)
    {
        const auto rows = _invalidationGrid.getRowCount();
        const auto columns = _invalidationGrid.getColumnCount();

        _dirtyVisualsTime.resize(rows * columns);

        uint32_t i = y * _invalidationGrid.getColumnCount() + x;
        if (_dirtyVisualsTime.size() > i)
        {
            _dirtyVisualsTime[i] = value;
        }
    }

    void RenderDirtyVisuals(int windowWidth, int windowHeight)
    {
        if (_width == 0 || _height == 0)
        {
            return;
        }

        float scaleX = static_cast<float>(windowWidth) / static_cast<float>(_width);
        float scaleY = static_cast<float>(windowHeight) / static_cast<float>(_height);

        _device->SetFVF(kColorFVF);
        _device->SetTexture(0, nullptr);
        _device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        _device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        _device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        for (uint32_t y = 0; y < _invalidationGrid.getRowCount(); y++)
        {
            for (uint32_t x = 0; x < _invalidationGrid.getColumnCount(); x++)
            {
                const auto timeEnd = GetDirtyVisualTime(x, y);
                const auto timeLeft = gCurrentRealTimeTicks < timeEnd ? timeEnd - gCurrentRealTimeTicks : 0;
                if (timeLeft > 0)
                {
                    uint32_t alpha = (timeLeft * kDirtyRegionAlpha / kDirtyVisualTime) & 0xFF;
                    DWORD color = (alpha << 24) | 0x00FFFFFF;

                    float rx1 = x * _invalidationGrid.getBlockWidth() * scaleX - 0.5f;
                    float ry1 = y * _invalidationGrid.getBlockHeight() * scaleY - 0.5f;
                    float rx2 = rx1 + _invalidationGrid.getBlockWidth() * scaleX;
                    float ry2 = ry1 + _invalidationGrid.getBlockHeight() * scaleY;

                    ColorVertex quad[4] = {
                        { rx1, ry1, 0.0f, 1.0f, color },
                        { rx2, ry1, 0.0f, 1.0f, color },
                        { rx1, ry2, 0.0f, 1.0f, color },
                        { rx2, ry2, 0.0f, 1.0f, color },
                    };
                    _device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ColorVertex));
                }
            }
        }
        _device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
};

std::unique_ptr<IDrawingEngine> Ui::CreateD3D9DrawingEngine(IUiContext& uiContext)
{
    return std::make_unique<D3D9DrawingEngine>(uiContext);
}

#endif // _WIN32
