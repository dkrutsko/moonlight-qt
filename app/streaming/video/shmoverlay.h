#pragma once

#include <SDL.h>

class ShmOverlay {
public:
    ShmOverlay();
    ~ShmOverlay();

    // Create the shared memory buffer with the given stream dimensions.
    // The external overlay producer opens this buffer and writes into it.
    bool create(int width, int height);

    // Returns true if overlay data is available for rendering.
    bool hasNewFrame();

    // Get the active RGBA buffer pixels (only valid when hasNewFrame() returns true)
    const uint8_t* getPixels();

    int getWidth();
    int getHeight();

    void close();

private:
    static uint32_t atomicLoad(const void* addr);

    uint8_t* m_MappedData;
    size_t m_MappedSize;
    bool m_Created;

    int m_Width;
    int m_Height;
    int m_BufSize;

    uint32_t m_LastWriteIndex;

#ifdef _WIN32
    void* m_MapHandle;
#else
    int m_Fd;
#endif
};
