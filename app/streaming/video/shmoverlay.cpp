#include "shmoverlay.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define SHM_OVERLAY_NAME "/oasis_overlay"
#define SHM_HEADER_SIZE 24
#define SHM_RECONNECT_INTERVAL_MS 1000
#define SHM_STALE_TIMEOUT_MS 2000

ShmOverlay::ShmOverlay()
    : m_MappedData(nullptr),
      m_MappedSize(0),
      m_Connected(false),
      m_Width(0),
      m_Height(0),
      m_BufSize(0),
      m_LastWriteIndex(0),
      m_StaleStartTicks(0),
      m_LastReconnectAttempt(0)
#ifdef _WIN32
      , m_MapHandle(nullptr)
#else
      , m_Fd(-1)
#endif
{
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

bool ShmOverlay::tryOpen()
{
#ifdef _WIN32
    m_MapHandle = OpenFileMappingW(FILE_MAP_READ, FALSE, L"oasis_overlay");
    if (m_MapHandle == nullptr) {
        return false;
    }

    m_MappedData = (uint8_t*)MapViewOfFile(m_MapHandle, FILE_MAP_READ, 0, 0, 0);
    if (m_MappedData == nullptr) {
        CloseHandle(m_MapHandle);
        m_MapHandle = nullptr;
        return false;
    }

    // Query the mapped region size
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(m_MappedData, &mbi, sizeof(mbi)) == 0) {
        UnmapViewOfFile(m_MappedData);
        m_MappedData = nullptr;
        CloseHandle(m_MapHandle);
        m_MapHandle = nullptr;
        return false;
    }
    m_MappedSize = mbi.RegionSize;
#else
    m_Fd = shm_open(SHM_OVERLAY_NAME, O_RDONLY, 0);
    if (m_Fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(m_Fd, &st) < 0 || st.st_size <= SHM_HEADER_SIZE) {
        ::close(m_Fd);
        m_Fd = -1;
        return false;
    }

    m_MappedSize = st.st_size;
    m_MappedData = (uint8_t*)mmap(nullptr, m_MappedSize, PROT_READ, MAP_SHARED, m_Fd, 0);
    if (m_MappedData == MAP_FAILED) {
        m_MappedData = nullptr;
        ::close(m_Fd);
        m_Fd = -1;
        return false;
    }
#endif

    // Read dimensions from header
    m_Width = (int)*(uint32_t*)(m_MappedData + 4);
    m_Height = (int)*(uint32_t*)(m_MappedData + 8);

    if (m_Width <= 0 || m_Height <= 0 || m_Width > 16384 || m_Height > 16384) {
        close();
        return false;
    }

    m_BufSize = m_Width * m_Height * 4;

    // Validate total size
    size_t expectedSize = SHM_HEADER_SIZE + 2 * (size_t)m_BufSize;
    if (m_MappedSize < expectedSize) {
        close();
        return false;
    }

    m_Connected = true;
    m_LastWriteIndex = atomicLoad(m_MappedData);
    m_StaleStartTicks = 0;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "ShmOverlay: connected (%dx%d)",
                m_Width, m_Height);
    return true;
}

bool ShmOverlay::update()
{
    if (!m_Connected) {
        Uint32 now = SDL_GetTicks();
        if (now - m_LastReconnectAttempt < SHM_RECONNECT_INTERVAL_MS) {
            return false;
        }
        m_LastReconnectAttempt = now;
        return tryOpen();
    }

    // Check liveness via writeIndex changes
    uint32_t writeIndex = atomicLoad(m_MappedData);
    if (writeIndex != m_LastWriteIndex) {
        m_LastWriteIndex = writeIndex;
        m_StaleStartTicks = 0;
    }
    else {
        Uint32 now = SDL_GetTicks();
        if (m_StaleStartTicks == 0) {
            m_StaleStartTicks = now;
        }
        else if (now - m_StaleStartTicks > SHM_STALE_TIMEOUT_MS) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "ShmOverlay: producer stale, disconnecting");
            close();
            return false;
        }
    }

    return true;
}

const uint8_t* ShmOverlay::getPixels()
{
    if (!m_Connected) {
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

int ShmOverlay::getX()
{
    if (!m_Connected) {
        return 0;
    }
    return (int)*(int32_t*)(m_MappedData + 12);
}

int ShmOverlay::getY()
{
    if (!m_Connected) {
        return 0;
    }
    return (int)*(int32_t*)(m_MappedData + 16);
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
#endif
        m_MappedData = nullptr;
    }

    if (m_Connected) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ShmOverlay: disconnected");
    }

    m_MappedSize = 0;
    m_Connected = false;
    m_Width = 0;
    m_Height = 0;
    m_BufSize = 0;
    m_StaleStartTicks = 0;
}
