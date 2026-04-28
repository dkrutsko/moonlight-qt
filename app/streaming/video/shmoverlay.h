#pragma once

#include <SDL.h>

class ShmOverlay {
public:
    ShmOverlay();
    ~ShmOverlay();

    // Call each frame. Returns true if overlay data is available.
    bool update();

    // Get the active RGBA buffer pixels (only valid when update() returns true)
    const uint8_t* getPixels();

    int getWidth();
    int getHeight();
    int getX();
    int getY();

    void close();

private:
    bool tryOpen();
    static uint32_t atomicLoad(const void* addr);

    uint8_t* m_MappedData;
    size_t m_MappedSize;
    bool m_Connected;

    int m_Width;
    int m_Height;
    int m_BufSize;

    uint32_t m_LastWriteIndex;
    Uint32 m_StaleStartTicks;
    Uint32 m_LastReconnectAttempt;

#ifdef _WIN32
    void* m_MapHandle;
#else
    int m_Fd;
#endif
};
