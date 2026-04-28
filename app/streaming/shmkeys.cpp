#include "shmkeys.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <string.h>
#include <errno.h>

#ifdef Q_OS_DARWIN
#include <CoreGraphics/CoreGraphics.h>
#endif

#define SHM_KEYS_NAME "/oasis_keys"
#define SHM_KEYS_SIZE 41
#define SHM_KEYS_VERSION 1

#define SHM_KEYS_OFFSET_VERSION   0
#define SHM_KEYS_OFFSET_RESERVED  4
#define SHM_KEYS_OFFSET_KEYBOARD  8
#define SHM_KEYS_OFFSET_MOUSE    40

ShmKeys::ShmKeys()
    : m_MappedData(nullptr),
      m_MappedSize(0),
      m_Created(false)
#ifdef _WIN32
      , m_MapHandle(nullptr)
#endif
{
#ifndef _WIN32
    m_Fd = -1;
#endif
}

ShmKeys::~ShmKeys()
{
    close();
}

bool ShmKeys::create()
{
    if (m_Created) {
        return true;
    }

    m_MappedSize = SHM_KEYS_SIZE;

#ifdef _WIN32
    wchar_t name[] = L"oasis_keys";
    m_MapHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE,
                                     0, (DWORD)m_MappedSize,
                                     name);
    if (m_MapHandle == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmKeys: CreateFileMappingW failed (%lu)",
                     GetLastError());
        return false;
    }

    m_MappedData = (uint8_t*)MapViewOfFile(m_MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, m_MappedSize);
    if (m_MappedData == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmKeys: MapViewOfFile failed (%lu)",
                     GetLastError());
        CloseHandle(m_MapHandle);
        m_MapHandle = nullptr;
        return false;
    }
#else
    shm_unlink(SHM_KEYS_NAME);

    m_Fd = shm_open(SHM_KEYS_NAME, O_CREAT | O_RDWR, 0666);
    if (m_Fd < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmKeys: shm_open failed (%d)",
                     errno);
        return false;
    }

    if (ftruncate(m_Fd, m_MappedSize) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmKeys: ftruncate failed (%d)",
                     errno);
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_KEYS_NAME);
        return false;
    }

    m_MappedData = (uint8_t*)mmap(nullptr, m_MappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, 0);
    if (m_MappedData == MAP_FAILED) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmKeys: mmap failed (%d)",
                     errno);
        m_MappedData = nullptr;
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_KEYS_NAME);
        return false;
    }
#endif

    memset(m_MappedData, 0, m_MappedSize);
    *(uint32_t*)(m_MappedData + SHM_KEYS_OFFSET_VERSION) = SHM_KEYS_VERSION;

    m_Created = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "ShmKeys: created");
    return true;
}

void ShmKeys::setKey(uint8_t scancode, bool pressed)
{
    if (!m_Created) {
        return;
    }

    uint8_t byteIndex = scancode / 8;
    uint8_t bitMask = 1 << (scancode % 8);
    volatile uint8_t* byte = &m_MappedData[SHM_KEYS_OFFSET_KEYBOARD + byteIndex];

    if (pressed) {
        *byte |= bitMask;
    }
    else {
        *byte &= ~bitMask;
    }
}

void ShmKeys::setMouseButton(int button, bool pressed)
{
    if (!m_Created) {
        return;
    }

    // Map SDL button constants to bit positions
    uint8_t bit;
    switch (button) {
    case SDL_BUTTON_LEFT:   bit = 0; break;
    case SDL_BUTTON_MIDDLE: bit = 1; break;
    case SDL_BUTTON_RIGHT:  bit = 2; break;
    case SDL_BUTTON_X1:     bit = 3; break;
    case SDL_BUTTON_X2:     bit = 4; break;
    default: return;
    }

    volatile uint8_t* byte = &m_MappedData[SHM_KEYS_OFFSET_MOUSE];
    uint8_t mask = 1 << bit;

    if (pressed) {
        *byte |= mask;
    }
    else {
        *byte &= ~mask;
    }
}

void ShmKeys::pollMouseButtons()
{
    if (!m_Created) {
        return;
    }

    uint8_t state = 0;

    Uint32 buttons = SDL_GetGlobalMouseState(nullptr, nullptr);
    if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT))   state |= 0x01;
    if (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) state |= 0x02;
    if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT))  state |= 0x04;
    if (buttons & SDL_BUTTON(SDL_BUTTON_X1))     state |= 0x08;
    if (buttons & SDL_BUTTON(SDL_BUTTON_X2))     state |= 0x10;

#ifdef Q_OS_DARWIN
    // Also check CoreGraphics hardware state for buttons that
    // SDL may not track (e.g. X1/X2 without helper apps)
    if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, 3)) state |= 0x08;
    if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, 4)) state |= 0x10;
#endif

    volatile uint8_t* byte = &m_MappedData[SHM_KEYS_OFFSET_MOUSE];
    *byte = state;
}

void ShmKeys::clearAll()
{
    if (!m_Created) {
        return;
    }

    memset(m_MappedData + SHM_KEYS_OFFSET_KEYBOARD, 0, 32);
    m_MappedData[SHM_KEYS_OFFSET_MOUSE] = 0;
}

void ShmKeys::close()
{
    if (m_MappedData != nullptr) {
#ifdef _WIN32
        UnmapViewOfFile(m_MappedData);
        if (m_MapHandle != nullptr) {
            CloseHandle(m_MapHandle);
            m_MapHandle = nullptr;
        }
#else
        munmap(m_MappedData, m_MappedSize);
        if (m_Fd >= 0) {
            ::close(m_Fd);
            m_Fd = -1;
        }
        shm_unlink(SHM_KEYS_NAME);
#endif
        m_MappedData = nullptr;
    }

    if (m_Created) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ShmKeys: destroyed");
    }

    m_MappedSize = 0;
    m_Created = false;
}
