#pragma once

#include <SDL.h>

class ShmKeys {
public:
    ShmKeys();
    ~ShmKeys();

    bool create();

    void setKey(uint8_t scancode, bool pressed);
    void setMouseButton(int button, bool pressed);
    void addScroll(int32_t delta);
    void pollMouseButtons();
    void clearAll();

    void close();

private:
    uint8_t* m_MappedData;
    size_t m_MappedSize;
    bool m_Created;

#ifdef _WIN32
    void* m_MapHandle;
#else
    int m_Fd;
#endif
};
