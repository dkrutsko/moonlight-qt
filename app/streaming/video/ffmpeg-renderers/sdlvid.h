#pragma once

#include "renderer.h"
#include "swframemapper.h"
#include "streaming/video/shmoverlay.h"

#ifdef HAVE_CUDA
#include "cuda.h"
#endif

extern "C" {
#include <libswscale/swscale.h>
}

class SdlRenderer : public IFFmpegRenderer {
public:
    SdlRenderer();
    virtual ~SdlRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void prepareToRender() override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool isRenderThreadSupported() override;
    virtual bool isPixelFormatSupported(int videoFormat, enum AVPixelFormat pixelFormat) override;
    virtual bool testRenderFrame(AVFrame* frame) override;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO) override;

private:
    void renderOverlay(Overlay::OverlayType type);
    void renderShmOverlay(SDL_Rect* videoRect);

    static void ffNoopFree(void *opaque, uint8_t *data);

    int m_VideoFormat;
    SDL_Renderer* m_Renderer;
    SDL_Texture* m_Texture;
    int m_ColorSpace;
    SDL_Texture* m_OverlayTextures[Overlay::OverlayMax];
    SDL_Rect m_OverlayRects[Overlay::OverlayMax];

    ShmOverlay m_ShmOverlay;
    SDL_Texture* m_ShmOverlayTexture;
    int m_ShmOverlayTextureWidth;
    int m_ShmOverlayTextureHeight;

    // Used for CPU conversion of YUV to RGB if needed
    bool m_NeedsYuvToRgbConversion;
    SwsContext* m_SwsContext;
    AVFrame* m_RgbFrame;

    SwFrameMapper m_SwFrameMapper;

#ifdef HAVE_CUDA
    CUDAGLInteropHelper* m_CudaGLHelper;
#endif
};

