#include "shmoverlay.h"

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

#define SHM_OVERLAY_NAME "/oasis_overlay"
#define SHM_HEADER_SIZE 24

ShmOverlay::ShmOverlay()
    : m_MappedData(nullptr),
      m_MappedSize(0),
      m_Created(false),
      m_Width(0),
      m_Height(0),
      m_BufSize(0),
      m_LastWriteIndex(0)
#ifdef _WIN32
      , m_MapHandle(nullptr)
#endif
{
#ifndef _WIN32
    m_Fd = -1;
#endif
}

ShmOverlay::~ShmOverlay()
{
    close();
}

uint32_t ShmOverlay::atomicLoad(const void* addr)
{
    uint32_t val = *(volatile const uint32_t*)addr;
    SDL_MemoryBarrierAcquire();
    return val;
}

bool ShmOverlay::create(int width, int height)
{
    if (m_Created) {
        return true;
    }

    if (width <= 0 || height <= 0) {
        return false;
    }

    m_Width = width;
    m_Height = height;
    m_BufSize = m_Width * m_Height * 4;
    m_MappedSize = SHM_HEADER_SIZE + 2 * (size_t)m_BufSize;

#ifdef _WIN32
    wchar_t name[] = L"oasis_overlay";
    m_MapHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE,
                                     (DWORD)(m_MappedSize >> 32),
                                     (DWORD)(m_MappedSize & 0xFFFFFFFF),
                                     name);
    if (m_MapHandle == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmOverlay: CreateFileMappingW failed (%lu)",
                     GetLastError());
        return false;
    }

    m_MappedData = (uint8_t*)MapViewOfFile(m_MapHandle, FILE_MAP_ALL_ACCESS, 0, 0, m_MappedSize);
    if (m_MappedData == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmOverlay: MapViewOfFile failed (%lu)",
                     GetLastError());
        CloseHandle(m_MapHandle);
        m_MapHandle = nullptr;
        return false;
    }
#else
    // Remove any stale segment from a previous run
    shm_unlink(SHM_OVERLAY_NAME);

    m_Fd = shm_open(SHM_OVERLAY_NAME, O_CREAT | O_RDWR, 0666);
    if (m_Fd < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmOverlay: shm_open failed (%d)",
                     errno);
        return false;
    }

    if (ftruncate(m_Fd, m_MappedSize) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmOverlay: ftruncate failed (%d)",
                     errno);
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_OVERLAY_NAME);
        return false;
    }

    m_MappedData = (uint8_t*)mmap(nullptr, m_MappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_Fd, 0);
    if (m_MappedData == MAP_FAILED) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ShmOverlay: mmap failed (%d)",
                     errno);
        m_MappedData = nullptr;
        ::close(m_Fd);
        m_Fd = -1;
        shm_unlink(SHM_OVERLAY_NAME);
        return false;
    }
#endif

    // Zero the entire buffer and write the header
    memset(m_MappedData, 0, m_MappedSize);
    *(uint32_t*)(m_MappedData + 0) = 0;         // writeIndex
    *(uint32_t*)(m_MappedData + 4) = m_Width;    // width
    *(uint32_t*)(m_MappedData + 8) = m_Height;   // height
    *(int32_t*)(m_MappedData + 12) = 0;          // x
    *(int32_t*)(m_MappedData + 16) = 0;          // y
    *(uint32_t*)(m_MappedData + 20) = 0;         // dirty

    m_Created = true;
    m_LastWriteIndex = 0;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "ShmOverlay: created (%dx%d)",
                m_Width, m_Height);
    return true;
}

bool ShmOverlay::hasNewFrame()
{
    if (!m_Created) {
        return false;
    }

    uint32_t writeIndex = atomicLoad(m_MappedData);
    if (writeIndex != m_LastWriteIndex) {
        m_LastWriteIndex = writeIndex;
        return true;
    }

    return false;
}

const uint8_t* ShmOverlay::getPixels()
{
    if (!m_Created) {
        return nullptr;
    }

    uint32_t writeIndex = atomicLoad(m_MappedData);
    return m_MappedData + SHM_HEADER_SIZE + writeIndex * m_BufSize;
}

int ShmOverlay::getWidth()
{
    return m_Width;
}

int ShmOverlay::getHeight()
{
    return m_Height;
}

void ShmOverlay::close()
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
        shm_unlink(SHM_OVERLAY_NAME);
#endif
        m_MappedData = nullptr;
    }

    if (m_Created) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ShmOverlay: destroyed");
    }

    m_MappedSize = 0;
    m_Created = false;
    m_Width = 0;
    m_Height = 0;
    m_BufSize = 0;
}
