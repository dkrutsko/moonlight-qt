#pragma once

#include <SDL.h>

// Input command types
#define SHM_INPUT_KEY_DOWN          0x01
#define SHM_INPUT_KEY_UP            0x02
#define SHM_INPUT_MOUSE_MOVE_REL    0x10
#define SHM_INPUT_MOUSE_MOVE_ABS    0x11
#define SHM_INPUT_MOUSE_BUTTON_DOWN 0x20
#define SHM_INPUT_MOUSE_BUTTON_UP   0x21
#define SHM_INPUT_MOUSE_SCROLL_V    0x30
#define SHM_INPUT_MOUSE_SCROLL_H    0x31

#pragma pack(push, 1)
struct ShmInputEntry {
    uint8_t  type;
    uint8_t  flags;
    int16_t  x;
    int16_t  y;
    uint16_t reserved;
};
#pragma pack(pop)

class ShmInput {
public:
    ShmInput();
    ~ShmInput();

    // Create the shared memory ring buffer
    bool create(int streamWidth, int streamHeight);

    // Drain all pending input commands and dispatch them via Limelight
    void processInput();

    void close();

private:
    static uint32_t atomicLoad(const void* addr);

    uint8_t* m_MappedData;
    size_t m_MappedSize;
    bool m_Created;

    int m_StreamWidth;
    int m_StreamHeight;

    uint32_t m_Capacity;

#ifdef _WIN32
    void* m_MapHandle;
#else
    int m_Fd;
#endif
};
