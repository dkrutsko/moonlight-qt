#pragma once

#include <SDL.h>

class ShmOverlay {
public:
    ShmOverlay();
    ~ShmOverlay();

    // Create the shared memory buffer with the given stream dimensions.
    // The external overlay producer opens this buffer and writes into it.
    bool create(int width, int height);

    // Acquire the latest frame for reading. Returns the pixel buffer,
    // or nullptr if no frame is available. Call releaseFrame() when done.
    const uint8_t* acquireFrame();

    // Release the frame acquired by acquireFrame().
    void releaseFrame();

    int getWidth();
    int getHeight();

    void close();

private:
    static uint32_t atomicLoad(const void* addr);
    static void atomicStore(void* addr, uint32_t val);

    uint8_t* m_MappedData;
    size_t m_MappedSize;
    bool m_Created;

    int m_Width;
    int m_Height;
    int m_BufSize;

    uint32_t m_AcquiredIndex;

#ifdef _WIN32
    void* m_MapHandle;
#else
    int m_Fd;
#endif
};
