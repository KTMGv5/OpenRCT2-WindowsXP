/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "DrawingEngineFactory.hpp"

#include <SDL_hints.h>
#include <SDL_render.h>
#include <SDL_version.h>
#include <cmath>
#include <memory>
#include <openrct2/Diagnostic.h>
#include <openrct2/Game.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/Guard.hpp>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/X8DrawingEngine.h>
#include <openrct2/interface/Window.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/world/Weather.h>
#include <vector>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Ui;

class HardwareDisplayDrawingEngine final : public X8DrawingEngine
{
private:
    constexpr static uint32_t kDirtyVisualTime = 40;
    constexpr static uint32_t kDirtyRegionAlpha = 100;

    IUiContext& _uiContext;
    SDL_Window* _window = nullptr;
    SDL_Renderer* _sdlRenderer = nullptr;
    SDL_Texture* _screenTexture = nullptr;
    SDL_Texture* _scaledScreenTexture = nullptr;
    SDL_PixelFormat* _screenTextureFormat = nullptr;
    uint32_t _paletteHWMapped[256] = { 0 };
    uint32_t _lightPaletteHWMapped[256] = { 0 };
    GamePalette _lastPalette = {};
    bool _hasPalette = false;

    bool _useVsync = true;

    std::vector<uint32_t> _dirtyVisualsTime;

    bool smoothNN = false;

public:
    explicit HardwareDisplayDrawingEngine(IUiContext& uiContext)
        : X8DrawingEngine(uiContext)
        , _uiContext(uiContext)
    {
        _window = static_cast<SDL_Window*>(_uiContext.GetWindow());
    }

    ~HardwareDisplayDrawingEngine() override
    {
        if (_screenTexture != nullptr)
        {
            SDL_DestroyTexture(_screenTexture);
        }
        if (_scaledScreenTexture != nullptr)
        {
            SDL_DestroyTexture(_scaledScreenTexture);
        }
        SDL_FreeFormat(_screenTextureFormat);
        SDL_DestroyRenderer(_sdlRenderer);
    }

    void Initialise() override
    {
        _sdlRenderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | (_useVsync ? SDL_RENDERER_PRESENTVSYNC : 0));
        if (_sdlRenderer == nullptr)
        {
            _sdlRenderer = SDL_CreateRenderer(_window, -1, _useVsync ? SDL_RENDERER_PRESENTVSYNC : 0);
        }
        if (_sdlRenderer == nullptr)
        {
            _sdlRenderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_SOFTWARE);
        }
    }

    SDL_Texture* CreateScreenTexture(uint32_t& pixelFormat, SDL_TextureAccess access, uint32_t width, uint32_t height)
    {
        SDL_Texture* texture = SDL_CreateTexture(_sdlRenderer, pixelFormat, access, width, height);
        if (texture != nullptr)
        {
            return texture;
        }

        LOG_WARNING("Failed to create texture (%s), trying fallback formats...", SDL_GetError());

        SDL_RendererInfo rendererInfo = {};
        if (SDL_GetRendererInfo(_sdlRenderer, &rendererInfo) == 0)
        {
            for (uint32_t i = 0; i < rendererInfo.num_texture_formats; i++)
            {
                uint32_t altFormat = rendererInfo.texture_formats[i];
                if (altFormat != pixelFormat && !SDL_ISPIXELFORMAT_FOURCC(altFormat) && !SDL_ISPIXELFORMAT_INDEXED(altFormat))
                {
                    texture = SDL_CreateTexture(_sdlRenderer, altFormat, access, width, height);
                    if (texture != nullptr)
                    {
                        pixelFormat = altFormat;
                        return texture;
                    }
                }
            }
        }

        LOG_WARNING("Renderer cannot create required texture; falling back to SDL software renderer...");
        SDL_DestroyRenderer(_sdlRenderer);
        _sdlRenderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_SOFTWARE);
        if (_sdlRenderer != nullptr)
        {
            if (SDL_GetRendererInfo(_sdlRenderer, &rendererInfo) == 0)
            {
                for (uint32_t i = 0; i < rendererInfo.num_texture_formats; i++)
                {
                    uint32_t format = rendererInfo.texture_formats[i];
                    if (!SDL_ISPIXELFORMAT_FOURCC(format) && !SDL_ISPIXELFORMAT_INDEXED(format))
                    {
                        texture = SDL_CreateTexture(_sdlRenderer, format, access, width, height);
                        if (texture != nullptr)
                        {
                            pixelFormat = format;
                            return texture;
                        }
                    }
                }
            }
        }

        return nullptr;
    }

    void SetVSync(bool vsync) override
    {
        if (_useVsync != vsync)
        {
            _useVsync = vsync;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            SDL_RenderSetVSync(_sdlRenderer, vsync ? 1 : 0);
#else
            SDL_DestroyRenderer(_sdlRenderer);
            _screenTexture = nullptr;
            _scaledScreenTexture = nullptr;
            Initialise();
            Resize(_uiContext->GetWidth(), _uiContext->GetHeight());
#endif
        }
    }

    void Resize(uint32_t width, uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        if (_screenTexture != nullptr)
        {
            SDL_DestroyTexture(_screenTexture);
            _screenTexture = nullptr;
        }
        if (_scaledScreenTexture != nullptr)
        {
            SDL_DestroyTexture(_scaledScreenTexture);
            _scaledScreenTexture = nullptr;
        }
        SDL_FreeFormat(_screenTextureFormat);
        _screenTextureFormat = nullptr;

        SDL_RendererInfo rendererInfo = {};
        int32_t result = SDL_GetRendererInfo(_sdlRenderer, &rendererInfo);
        if (result < 0)
        {
            LOG_WARNING("HWDisplayDrawingEngine::Resize error: %s", SDL_GetError());
            return;
        }
        uint32_t pixelFormat = SDL_PIXELFORMAT_UNKNOWN;
        for (uint32_t i = 0; i < rendererInfo.num_texture_formats; i++)
        {
            uint32_t format = rendererInfo.texture_formats[i];
            if (!SDL_ISPIXELFORMAT_FOURCC(format) && !SDL_ISPIXELFORMAT_INDEXED(format)
                && (pixelFormat == SDL_PIXELFORMAT_UNKNOWN || SDL_BYTESPERPIXEL(format) < SDL_BYTESPERPIXEL(pixelFormat)))
            {
                pixelFormat = format;
            }
        }

        ScaleQuality scaleQuality = GetContext()->GetUiContext().GetScaleQuality();
        if (scaleQuality == ScaleQuality::smoothNearestNeighbour)
        {
            scaleQuality = ScaleQuality::linear;
            smoothNN = true;
        }
        else
        {
            smoothNN = false;
        }

        if (smoothNN)
        {
            char scaleQualityBuffer[4];
            snprintf(scaleQualityBuffer, sizeof(scaleQualityBuffer), "%d", static_cast<int32_t>(scaleQuality));
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

            _screenTexture = CreateScreenTexture(pixelFormat, SDL_TEXTUREACCESS_STREAMING, width, height);

            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scaleQualityBuffer);

            uint32_t scale = std::ceil(Config::Get().general.windowScale);
            _scaledScreenTexture = SDL_CreateTexture(
                _sdlRenderer, pixelFormat, SDL_TEXTUREACCESS_TARGET, width * scale, height * scale);

            if (_scaledScreenTexture == nullptr)
            {
                LOG_WARNING("RenderTarget texture creation failed, disabling smooth nearest neighbour: %s", SDL_GetError());
                smoothNN = false;
            }
        }
        else
        {
            _screenTexture = CreateScreenTexture(pixelFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
        }

        Guard::Assert(
            _screenTexture != nullptr, "Failed to create screen texture (%ux%u, pixelFormat = %u): %s", width, height,
            pixelFormat, SDL_GetError());

        uint32_t format;
        SDL_QueryTexture(_screenTexture, &format, nullptr, nullptr, nullptr);
        _screenTextureFormat = SDL_AllocFormat(format);

        if (_hasPalette)
        {
            SetPalette(_lastPalette);
        }

        X8DrawingEngine::Resize(width, height);
    }

    void SetPalette(const GamePalette& palette) override
    {
        _lastPalette = palette;
        _hasPalette = true;

        if (_screenTextureFormat != nullptr)
        {
            for (int32_t i = 0; i < 256; i++)
            {
                _paletteHWMapped[i] = SDL_MapRGB(_screenTextureFormat, palette[i].red, palette[i].green, palette[i].blue);
            }

            if (Config::Get().general.enableLightFx)
            {
                auto& lightPalette = LightFx::GetPalette();
                for (int32_t i = 0; i < 256; i++)
                {
                    const auto& src = lightPalette[i];
                    _lightPaletteHWMapped[i] = SDL_MapRGBA(_screenTextureFormat, src.red, src.green, src.blue, src.alpha);
                }
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
        float night = static_cast<float>(pow(gDayNightCycle, 1.5));
        if ((Config::Get().general.enableLightFx && night > 0.001f) || Weather::hasWeatherEffect() || gPaintForceRedraw)
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
    void Display()
    {
        auto* viewport = WindowGetViewport(WindowGetMain());

        if (Config::Get().general.enableLightFx && viewport != nullptr)
        {
            void* pixels;
            int32_t pitch;
            if (SDL_LockTexture(_screenTexture, nullptr, &pixels, &pitch) == 0)
            {
                LightFx::RenderToTexture(
                    *viewport, pixels, pitch, _bits, _width, _height, _paletteHWMapped, _lightPaletteHWMapped);
                SDL_UnlockTexture(_screenTexture);
            }
        }
        else
        {
            CopyBitsToTexture(
                _screenTexture, _bits, static_cast<int32_t>(_width), static_cast<int32_t>(_height), _paletteHWMapped);
        }
        if (smoothNN)
        {
            SDL_SetRenderTarget(_sdlRenderer, _scaledScreenTexture);
            SDL_RenderCopy(_sdlRenderer, _screenTexture, nullptr, nullptr);

            SDL_SetRenderTarget(_sdlRenderer, nullptr);
            SDL_RenderCopy(_sdlRenderer, _scaledScreenTexture, nullptr, nullptr);
        }
        else
        {
            SDL_RenderCopy(_sdlRenderer, _screenTexture, nullptr, nullptr);
        }

        if (gShowDirtyVisuals)
        {
            RenderDirtyVisuals();
        }

        SDL_RenderPresent(_sdlRenderer);
    }

    void CopyBitsToTexture(SDL_Texture* texture, PaletteIndex* src, int32_t width, int32_t height, const uint32_t* palette)
    {
        void* pixels;
        int32_t pitch;
        if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) == 0)
        {
            const uint32_t bpp = _screenTextureFormat != nullptr ? _screenTextureFormat->BytesPerPixel : 4;
            if (bpp == 4)
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
                    for (int32_t y = 0; y < height; y++)
                    {
                        uint32_t* dst = reinterpret_cast<uint32_t*>(rowDst);
                        int32_t blocks = width / 4;
                        int32_t remainder = width % 4;
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
                        rowDst += pitch;
                    }
                }
            }
            else if (bpp == 2)
            {
                uint8_t* rowDst = static_cast<uint8_t*>(pixels);
                for (int32_t y = 0; y < height; y++)
                {
                    uint16_t* dst = reinterpret_cast<uint16_t*>(rowDst);
                    int32_t blocks = width / 4;
                    int32_t remainder = width % 4;
                    while (blocks-- > 0)
                    {
                        dst[0] = static_cast<uint16_t>(palette[EnumValue(src[0])]);
                        dst[1] = static_cast<uint16_t>(palette[EnumValue(src[1])]);
                        dst[2] = static_cast<uint16_t>(palette[EnumValue(src[2])]);
                        dst[3] = static_cast<uint16_t>(palette[EnumValue(src[3])]);
                        src += 4;
                        dst += 4;
                    }
                    while (remainder-- > 0)
                    {
                        *dst++ = static_cast<uint16_t>(palette[EnumValue(*src++)]);
                    }
                    rowDst += pitch;
                }
            }
            SDL_UnlockTexture(texture);
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

    void RenderDirtyVisuals()
    {
        int windowX, windowY, renderX, renderY;
        SDL_GetWindowSize(_window, &windowX, &windowY);
        SDL_GetRendererOutputSize(_sdlRenderer, &renderX, &renderY);

        float scaleX = Config::Get().general.windowScale * renderX / static_cast<float>(windowX);
        float scaleY = Config::Get().general.windowScale * renderY / static_cast<float>(windowY);

        SDL_SetRenderDrawBlendMode(_sdlRenderer, SDL_BLENDMODE_BLEND);
        for (uint32_t y = 0; y < _invalidationGrid.getRowCount(); y++)
        {
            for (uint32_t x = 0; x < _invalidationGrid.getColumnCount(); x++)
            {
                const auto timeEnd = GetDirtyVisualTime(x, y);
                const auto timeLeft = gCurrentRealTimeTicks < timeEnd ? timeEnd - gCurrentRealTimeTicks : 0;
                if (timeLeft > 0)
                {
                    uint8_t alpha = timeLeft * kDirtyRegionAlpha / kDirtyVisualTime;
                    SDL_Rect ddRect;
                    ddRect.x = static_cast<int32_t>(x * _invalidationGrid.getBlockWidth() * scaleX);
                    ddRect.y = static_cast<int32_t>(y * _invalidationGrid.getBlockHeight() * scaleY);
                    ddRect.w = static_cast<int32_t>(_invalidationGrid.getBlockWidth() * scaleX);
                    ddRect.h = static_cast<int32_t>(_invalidationGrid.getBlockHeight() * scaleY);

                    SDL_SetRenderDrawColor(_sdlRenderer, 255, 255, 255, alpha);
                    SDL_RenderFillRect(_sdlRenderer, &ddRect);
                }
            }
        }
    }
};

std::unique_ptr<IDrawingEngine> Ui::CreateHardwareDisplayDrawingEngine(IUiContext& uiContext)
{
    return std::make_unique<HardwareDisplayDrawingEngine>(uiContext);
}
