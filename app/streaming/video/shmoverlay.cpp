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
#define SHM_HEADER_SIZE 32
#define SHM_NUM_BUFFERS 3

#define SHM_OFFSET_WIDTH    0
#define SHM_OFFSET_HEIGHT   4
#define SHM_OFFSET_READY    8
#define SHM_OFFSET_READING 12

#define SHM_READING_IDLE 0xFF

ShmOverlay::ShmOverlay()
    : m_MappedData(nullptr),
      m_MappedSize(0),
      m_Created(false),
      m_Width(0),
      m_Height(0),
      m_BufSize(0),
      m_AcquiredIndex(SHM_READING_IDLE)
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

void ShmOverlay::atomicStore(void* addr, uint32_t val)
{
    SDL_MemoryBarrierRelease();
    *(volatile uint32_t*)addr = val;
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
    m_MappedSize = SHM_HEADER_SIZE + SHM_NUM_BUFFERS * (size_t)m_BufSize;

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
    *(uint32_t*)(m_MappedData + SHM_OFFSET_WIDTH) = m_Width;
    *(uint32_t*)(m_MappedData + SHM_OFFSET_HEIGHT) = m_Height;
    atomicStore(m_MappedData + SHM_OFFSET_READY, 0);
    atomicStore(m_MappedData + SHM_OFFSET_READING, SHM_READING_IDLE);

    m_Created = true;
    m_AcquiredIndex = SHM_READING_IDLE;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "ShmOverlay: created (%dx%d, triple-buffered)",
                m_Width, m_Height);
    return true;
}

const uint8_t* ShmOverlay::acquireFrame()
{
    if (!m_Created) {
        return nullptr;
    }

    uint32_t readyIdx = atomicLoad(m_MappedData + SHM_OFFSET_READY);
    if (readyIdx >= SHM_NUM_BUFFERS) {
        return nullptr;
    }

    m_AcquiredIndex = readyIdx;
    atomicStore(m_MappedData + SHM_OFFSET_READING, readyIdx);

    return m_MappedData + SHM_HEADER_SIZE + readyIdx * m_BufSize;
}

void ShmOverlay::releaseFrame()
{
    if (!m_Created) {
        return;
    }

    m_AcquiredIndex = SHM_READING_IDLE;
    atomicStore(m_MappedData + SHM_OFFSET_READING, SHM_READING_IDLE);
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
        // Release any held frame before unmapping
        if (m_AcquiredIndex != SHM_READING_IDLE) {
            releaseFrame();
        }

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
