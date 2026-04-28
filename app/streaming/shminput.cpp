#include "shminput.h"

#include <Limelight.h>

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

#define SHM_INPUT_NAME "/oasis_input"
#define SHM_INPUT_HEADER_SIZE 16
#define SHM_INPUT_CAPACITY 256

ShmInput::ShmInput()
    : m_MappedData(nullptr),
      m_MappedSize(0),
      m_Created(false),
      m_StreamWidth(0),
      m_StreamHeight(0),
      m_Capacity(0)
#ifdef _WIN32
      , m_MapHandle(nullptr)
#endif
{
#ifndef _WIN32
    m_Fd = -1;
#endif
}

ShmInput::~ShmInput()
{
    close();
}

uint32_t ShmInput::atomicLoad(const void* addr)
{
    uint32_t val = *(volatile const uint32_t*)addr;
    SDL_MemoryBarrierAcquire();
    return val;
}

bool ShmInput::create(int streamWidth, int streamHeight)
{
    if (m_Created) {
        return true;
    }

    m_StreamWidth = streamWidth;
    m_StreamHeight = streamHeight;
    m_Capacity = SHM_INPUT_CAPACITY;
    m_MappedSize = SHM_INPUT_HEADER_SIZE + m_Capacity * sizeof(ShmInputEntry);

#ifdef _WIN32
    wchar_t name[] = L"oasis_input";
    m_MapHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE,
                                     0, (DWORD)m_MappedSize,
                                     name);
    if (m_MapHandle == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmInput: CreateFileMappingW failed (%lu)",
                     GetLastError());
        return false;
    }

    m_MappedData = (uint8_t*)MapViewOfFile(m_MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, m_MappedSize);
    if (m_MappedData == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmInput: MapViewOfFile failed (%lu)",
                     GetLastError());
        CloseHandle(m_MapHandle);
        m_MapHandle = nullptr;
        return false;
    }
#else
    shm_unlink(SHM_INPUT_NAME);

    m_Fd = shm_open(SHM_INPUT_NAME, O_CREAT | O_RDWR, 0666);
    if (m_Fd < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmInput: shm_open failed (%d)",
                     errno);
        return false;
    }

    if (ftruncate(m_Fd, m_MappedSize) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmInput: ftruncate failed (%d)",
                     errno);
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_INPUT_NAME);
        return false;
    }

    m_MappedData = (uint8_t*)mmap(nullptr, m_MappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, 0);
    if (m_MappedData == MAP_FAILED) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmInput: mmap failed (%d)",
                     errno);
        m_MappedData = nullptr;
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_INPUT_NAME);
        return false;
    }
#endif

    // Initialize header
    memset(m_MappedData, 0, m_MappedSize);
    *(uint32_t*)(m_MappedData + 0) = m_Capacity;
    *(uint32_t*)(m_MappedData + 4) = (uint32_t)sizeof(ShmInputEntry);
    *(uint32_t*)(m_MappedData + 8) = 0;   // writePos
    *(uint32_t*)(m_MappedData + 12) = 0;  // readPos

    m_Created = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "ShmInput: created (capacity=%u, stream=%dx%d)",
                m_Capacity, m_StreamWidth, m_StreamHeight);
    return true;
}

void ShmInput::processInput()
{
    if (!m_Created) {
        return;
    }

    uint32_t writePos = atomicLoad(m_MappedData + 8);
    uint32_t readPos = atomicLoad(m_MappedData + 12);

    ShmInputEntry* entries = (ShmInputEntry*)(m_MappedData + SHM_INPUT_HEADER_SIZE);

    while (readPos != writePos) {
        ShmInputEntry* entry = &entries[readPos % m_Capacity];

        switch (entry->type) {
        case SHM_INPUT_KEY_DOWN:
            LiSendKeyboardEvent(entry->x, KEY_ACTION_DOWN, entry->flags);
            break;

        case SHM_INPUT_KEY_UP:
            LiSendKeyboardEvent(entry->x, KEY_ACTION_UP, entry->flags);
            break;

        case SHM_INPUT_MOUSE_MOVE_REL:
            LiSendMouseMoveEvent(entry->x, entry->y);
            break;

        case SHM_INPUT_MOUSE_MOVE_ABS:
            LiSendMousePositionEvent(entry->x, entry->y,
                                     (short)m_StreamWidth, (short)m_StreamHeight);
            break;

        case SHM_INPUT_MOUSE_BUTTON_DOWN:
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, entry->flags);
            break;

        case SHM_INPUT_MOUSE_BUTTON_UP:
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, entry->flags);
            break;

        case SHM_INPUT_MOUSE_SCROLL_V:
            LiSendHighResScrollEvent(entry->x);
            break;

        case SHM_INPUT_MOUSE_SCROLL_H:
            LiSendHighResHScrollEvent(entry->x);
            break;
        }

        readPos++;
    }

    // Atomically update readPos so the producer sees consumed entries
    SDL_MemoryBarrierRelease();
    *(volatile uint32_t*)(m_MappedData + 12) = readPos;
}

void ShmInput::close()
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
        shm_unlink(SHM_INPUT_NAME);
#endif
        m_MappedData = nullptr;
    }

    if (m_Created) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ShmInput: destroyed");
    }

    m_MappedSize = 0;
    m_Created = false;
    m_Capacity = 0;
}
