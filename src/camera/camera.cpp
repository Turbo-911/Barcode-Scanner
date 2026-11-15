#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32) && defined(__has_include)
#if __has_include(<mfapi.h>) && __has_include(<mfidl.h>) && __has_include(<mfreadwrite.h>) && __has_include(<mferror.h>)
#define BARCODE_HAS_MEDIA_FOUNDATION 1
#endif
#endif

#if defined(BARCODE_HAS_MEDIA_FOUNDATION)
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#endif

#include <iostream>
#include <cstring>
#include <iomanip>

#include "camera.h"

#if defined(BARCODE_HAS_MEDIA_FOUNDATION)

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
const char* hresultDescription(HRESULT hr)
{
    switch (hr)
    {
    case S_OK: return "success";
    case S_FALSE: return "success with a false result";
    case E_ACCESSDENIED: return "access denied; check camera privacy permissions";
    case E_INVALIDARG: return "invalid argument";
    case E_OUTOFMEMORY: return "out of memory";
    case MF_E_NO_CAPTURE_DEVICES_AVAILABLE: return "no capture devices are available";
    case MF_E_NOT_INITIALIZED: return "Media Foundation is not initialized";
    case MF_E_PLATFORM_NOT_INITIALIZED: return "Media Foundation platform is not initialized";
    case MF_E_UNSUPPORTED_BYTESTREAM_TYPE: return "unsupported byte-stream type";
    case MF_E_INVALIDMEDIATYPE: return "invalid or unsupported media type";
    case MF_E_NOT_FOUND: return "requested item was not found";
    case RPC_E_CHANGED_MODE: return "COM was already initialized with a different apartment model";
    default: return "see HRESULT documentation for details";
    }
}

void logHresult(const char* operation, HRESULT hr)
{
    std::cerr << "[Camera] " << operation << " failed. HRESULT=0x"
              << std::hex << std::setw(8) << std::setfill('0')
              << static_cast<unsigned long>(hr) << std::dec
              << " (" << hresultDescription(hr) << ")\n";
}

void logDeviceName(IMFActivate* device, UINT32 index)
{
    WCHAR* name = NULL;
    UINT32 nameLength = 0;
    HRESULT hr = device->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
    if (SUCCEEDED(hr) && name != NULL)
    {
        std::wcerr << L"[Camera] Device " << index << L": " << name << L"\n";
        CoTaskMemFree(name);
    }
    else
    {
        logHresult("IMFActivate::GetAllocatedString(FRIENDLY_NAME)", hr);
    }
}
}

Camera::Camera()
    : m_pSourceReader(NULL)
    , m_width(0)
    , m_height(0)
    , m_stride(0)
    , m_formatFourCC(0)
    , m_isInitialized(false)
    , m_comInitialized(false)
    , m_mfStarted(false)
{
}

Camera::~Camera()
{
    cleanup();
}

bool Camera::initialize()
{
    if (m_isInitialized)
    {
        return true;
    }

    // Initialize COM library
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE)
    {
        m_comInitialized = true;
    }
    else if (hr == RPC_E_CHANGED_MODE)
    {
        std::cerr << "[Camera] CoInitializeEx returned HRESULT=0x"
                  << std::hex << static_cast<unsigned long>(hr) << std::dec
                  << " (" << hresultDescription(hr)
                  << "); continuing with the existing COM apartment.\n";
        m_comInitialized = false;
    }
    else
    {
        logHresult("CoInitializeEx", hr);
        cleanup();
        return false;
    }

    // Initialize Windows Media Foundation runtime
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr))
    {
        logHresult("MFStartup", hr);
        cleanup();
        return false;
    }
    m_mfStarted = true;

    // Enumerate video capture devices
    IMFAttributes* pAttributes = NULL;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr) || pAttributes == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("MFCreateAttributes(device enumeration)", hr);
        cleanup();
        return false;
    }

    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    );
    if (FAILED(hr))
    {
        logHresult("IMFAttributes::SetGUID(SOURCE_TYPE_VIDCAP)", hr);
        pAttributes->Release();
        cleanup();
        return false;
    }

    IMFActivate** ppDevices = NULL;
    UINT32 deviceCount = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &deviceCount);
    pAttributes->Release();
    pAttributes = NULL;

    if (FAILED(hr))
    {
        logHresult("MFEnumDeviceSources", hr);
        cleanup();
        return false;
    }

    if (deviceCount == 0 || ppDevices == NULL)
    {
        std::cerr << "[Camera] MFEnumDeviceSources returned no video capture devices.\n";
        cleanup();
        return false;
    }

    std::cerr << "[Camera] Enumerated " << deviceCount << " video capture device(s).\n";
    for (UINT32 i = 0; i < deviceCount; ++i)
    {
        if (ppDevices[i] != NULL)
        {
            logDeviceName(ppDevices[i], i);
        }
    }

    // Activate the primary capture device
    IMFMediaSource* pMediaSource = NULL;
    if (ppDevices[0] == NULL)
    {
        std::cerr << "[Camera] Device 0 is null; cannot activate a capture source.\n";
        CoTaskMemFree(ppDevices);
        cleanup();
        return false;
    }
    hr = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pMediaSource));
    if (FAILED(hr))
    {
        logHresult("IMFActivate::ActivateObject(IMFMediaSource)", hr);
    }

    // Release all device activator objects
    for (UINT32 i = 0; i < deviceCount; ++i)
    {
        if (ppDevices[i] != NULL)
        {
            ppDevices[i]->Release();
            ppDevices[i] = NULL;
        }
    }
    CoTaskMemFree(ppDevices);
    ppDevices = NULL;

    if (FAILED(hr) || pMediaSource == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        std::cerr << "[Camera] Webcam activation failed; the device may be busy or blocked by privacy settings.\n";
        cleanup();
        return false;
    }

    // Configure source reader attributes
    IMFAttributes* pReaderAttributes = NULL;
    hr = MFCreateAttributes(&pReaderAttributes, 2);
    if (FAILED(hr) || pReaderAttributes == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("MFCreateAttributes(source reader)", hr);
        if (pMediaSource != NULL) pMediaSource->Release();
        cleanup();
        return false;
    }
    else
    {
        hr = pReaderAttributes->SetUINT32(
            MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (FAILED(hr))
        {
            logHresult("IMFAttributes::SetUINT32(ENABLE_VIDEO_PROCESSING)", hr);
            pReaderAttributes->Release();
            pMediaSource->Release();
            cleanup();
            return false;
        }
        hr = pReaderAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        if (FAILED(hr))
        {
            logHresult("IMFAttributes::SetUINT32(LOW_LATENCY)", hr);
            pReaderAttributes->Release();
            pMediaSource->Release();
            cleanup();
            return false;
        }
    }

    IMFSourceReader* pReader = NULL;
    hr = MFCreateSourceReaderFromMediaSource(pMediaSource, pReaderAttributes, &pReader);

    if (pReaderAttributes != NULL)
    {
        pReaderAttributes->Release();
        pReaderAttributes = NULL;
    }

    if (pMediaSource != NULL)
    {
        pMediaSource->Release();
        pMediaSource = NULL;
    }

    if (FAILED(hr) || pReader == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("MFCreateSourceReaderFromMediaSource", hr);
        cleanup();
        return false;
    }

    m_pSourceReader = pReader;

    // Attempt to set output media type to uncompressed RGB32
    IMFMediaType* pPartialType = NULL;
    hr = MFCreateMediaType(&pPartialType);
    if (FAILED(hr) || pPartialType == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("MFCreateMediaType(RGB32 request)", hr);
        cleanup();
        return false;
    }
    else
    {
        hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr))
        {
            logHresult("IMFMediaType::SetGUID(MAJOR_TYPE)", hr);
            pPartialType->Release();
            cleanup();
            return false;
        }
        hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(hr))
        {
            logHresult("IMFMediaType::SetGUID(RGB32)", hr);
            pPartialType->Release();
            cleanup();
            return false;
        }
        hr = m_pSourceReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pPartialType);
        if (FAILED(hr))
        {
            logHresult("IMFSourceReader::SetCurrentMediaType(RGB32)", hr);
            std::cerr << "[Camera] RGB32 negotiation failed; inspecting the native camera format.\n";
        }
        pPartialType->Release();
        pPartialType = NULL;
    }

    // Inspect negotiated media type
    IMFMediaType* pCurrentType = NULL;
    hr = m_pSourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &pCurrentType
    );
    if (FAILED(hr) || pCurrentType == NULL)
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("IMFSourceReader::GetCurrentMediaType", hr);
        cleanup();
        return false;
    }

    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        if (SUCCEEDED(hr)) hr = MF_E_INVALIDMEDIATYPE;
        logHresult("MFGetAttributeSize(FRAME_SIZE)", hr);
        pCurrentType->Release();
        cleanup();
        return false;
    }

    GUID subtype = GUID_NULL;
    hr = pCurrentType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
    {
        logHresult("IMFMediaType::GetGUID(SUBTYPE)", hr);
        pCurrentType->Release();
        cleanup();
        return false;
    }

    LONG stride = 0;
    hr = pCurrentType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride);
    if (FAILED(hr))
    {
        if (subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_ARGB32)
        {
            stride = (LONG)(width * 4);
        }
        else if (subtype == MFVideoFormat_RGB24)
        {
            stride = (LONG)(((width * 3) + 3) & ~3);
        }
        else if (subtype == MFVideoFormat_YUY2)
        {
            stride = (LONG)(width * 2);
        }
        else if (subtype == MFVideoFormat_NV12)
        {
            stride = (LONG)width;
        }
        else
        {
            stride = (LONG)(width * 4);
        }
    }

    pCurrentType->Release();
    pCurrentType = NULL;

    m_width = (int)width;
    m_height = (int)height;
    m_stride = stride;
    m_formatFourCC = subtype.Data1;
    m_isInitialized = true;

    std::cerr << "[Camera] Camera initialized: " << m_width << "x"
              << m_height << ", stride " << m_stride << ".\n";
    return true;
}

void Camera::cleanup()
{
    if (m_pSourceReader != NULL)
    {
        m_pSourceReader->Release();
        m_pSourceReader = NULL;
    }

    if (m_mfStarted)
    {
        MFShutdown();
        m_mfStarted = false;
    }

    if (m_comInitialized)
    {
        CoUninitialize();
        m_comInitialized = false;
    }

    m_width = 0;
    m_height = 0;
    m_stride = 0;
    m_formatFourCC = 0;
    m_isInitialized = false;
}

bool Camera::isInitialized() const
{
    return m_isInitialized;
}

unsigned char* Camera::readFrameRaw(int& outWidth, int& outHeight)
{
    outWidth = 0;
    outHeight = 0;

    if (!m_isInitialized || m_pSourceReader == NULL)
    {
        return nullptr;
    }

    DWORD streamFlags = 0;
    LONGLONG timestamp = 0;
    IMFSample* pSample = NULL;
    HRESULT hr = S_OK;

    // Retry loop for stream stabilization and skipping stream ticks
    for (int retry = 0; retry < 15 && pSample == NULL; ++retry)
    {
        hr = m_pSourceReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            NULL,
            &streamFlags,
            &timestamp,
            &pSample
        );

        if (FAILED(hr))
        {
            logHresult("IMFSourceReader::ReadSample", hr);
            return nullptr;
        }

        if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            std::cerr << "[Camera Warning] Reached end of video stream.\n";
            return nullptr;
        }

        if (pSample == NULL)
        {
            Sleep(15);
        }
    }

    if (pSample == NULL)
    {
        std::cerr << "[Camera] ReadSample returned no frame after 15 attempts.\n";
        return nullptr;
    }

    IMFMediaBuffer* pMediaBuffer = NULL;
    hr = pSample->ConvertToContiguousBuffer(&pMediaBuffer);
    if (FAILED(hr) || pMediaBuffer == NULL)
    {
        pSample->Release();
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("IMFSample::ConvertToContiguousBuffer", hr);
        return nullptr;
    }

    BYTE* pData = NULL;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = pMediaBuffer->Lock(&pData, &maxLength, &currentLength);
    if (FAILED(hr) || pData == NULL)
    {
        pMediaBuffer->Release();
        pSample->Release();
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        logHresult("IMFMediaBuffer::Lock", hr);
        return nullptr;
    }

    int w = m_width;
    int h = m_height;
    int bufferSize = w * h;

    // Strictly manual memory allocation for the grayscale pixel buffer
    unsigned char* pGrayscale = new unsigned char[bufferSize];

    LONG absStride = (m_stride < 0) ? -m_stride : m_stride;

    if (m_formatFourCC == MFVideoFormat_RGB32.Data1 || m_formatFourCC == MFVideoFormat_ARGB32.Data1)
    {
        for (int y = 0; y < h; ++y)
        {
            const BYTE* pRow = pData + (y * absStride);
            for (int x = 0; x < w; ++x)
            {
                BYTE b = pRow[x * 4 + 0];
                BYTE g = pRow[x * 4 + 1];
                BYTE r = pRow[x * 4 + 2];
                pGrayscale[y * w + x] = (unsigned char)((299 * (int)r + 587 * (int)g + 114 * (int)b) / 1000);
            }
        }
    }
    else if (m_formatFourCC == MFVideoFormat_YUY2.Data1)
    {
        for (int y = 0; y < h; ++y)
        {
            const BYTE* pRow = pData + (y * absStride);
            for (int x = 0; x < w; x += 2)
            {
                int block = (x / 2) * 4;
                pGrayscale[y * w + x] = pRow[block + 0];
                if (x + 1 < w)
                {
                    pGrayscale[y * w + x + 1] = pRow[block + 2];
                }
            }
        }
    }
    else if (m_formatFourCC == MFVideoFormat_NV12.Data1)
    {
        for (int y = 0; y < h; ++y)
        {
            const BYTE* pRow = pData + (y * absStride);
            for (int x = 0; x < w; ++x)
            {
                pGrayscale[y * w + x] = pRow[x];
            }
        }
    }
    else if (m_formatFourCC == MFVideoFormat_RGB24.Data1)
    {
        for (int y = 0; y < h; ++y)
        {
            const BYTE* pRow = pData + (y * absStride);
            for (int x = 0; x < w; ++x)
            {
                BYTE b = pRow[x * 3 + 0];
                BYTE g = pRow[x * 3 + 1];
                BYTE r = pRow[x * 3 + 2];
                pGrayscale[y * w + x] = (unsigned char)((299 * (int)r + 587 * (int)g + 114 * (int)b) / 1000);
            }
        }
    }
    else
    {
        DWORD copyCount = (currentLength < (DWORD)bufferSize) ? currentLength : (DWORD)bufferSize;
        for (DWORD i = 0; i < copyCount; ++i)
        {
            pGrayscale[i] = pData[i];
        }
        for (int i = (int)copyCount; i < bufferSize; ++i)
        {
            pGrayscale[i] = 255;
        }
    }

    pMediaBuffer->Unlock();
    pMediaBuffer->Release();
    pSample->Release();

    outWidth = w;
    outHeight = h;
    return pGrayscale;
}

bool Camera::captureFrame(Image& out)
{
    int w = 0;
    int h = 0;
    unsigned char* rawPixels = readFrameRaw(w, h);
    if (rawPixels == nullptr)
    {
        return false;
    }

    int totalPixels = w * h;
    out.width = w;
    out.height = h;
    out.pixels.resize((size_t)totalPixels);
    std::memcpy(out.pixels.data(), rawPixels, (size_t)totalPixels);

    // Free the manually allocated raw buffer
    delete[] rawPixels;
    rawPixels = nullptr;

    return true;
}

bool Camera::captureFrame(Image* out)
{
    if (out == nullptr)
    {
        return false;
    }
    return captureFrame(*out);
}

Image* Camera::captureImage()
{
    int w = 0;
    int h = 0;
    unsigned char* rawPixels = readFrameRaw(w, h);
    if (rawPixels == nullptr)
    {
        return nullptr;
    }

    int totalPixels = w * h;
    Image* pImg = new Image();
    pImg->width = w;
    pImg->height = h;
    pImg->pixels.resize((size_t)totalPixels);
    std::memcpy(pImg->pixels.data(), rawPixels, (size_t)totalPixels);

    // Free the manually allocated raw buffer
    delete[] rawPixels;
    rawPixels = nullptr;

    return pImg;
}

unsigned char* Camera::captureRawGrayscale(int& width, int& height)
{
    return readFrameRaw(width, height);
}

bool Camera::captureSingleFrame(Image& out)
{
    Camera cam;
    if (!cam.initialize())
    {
        return false;
    }

    bool success = cam.captureFrame(out);
    cam.cleanup();
    return success;
}

namespace
{
    Camera s_globalCamera;
}

bool captureFrame(Image& out)
{
    if (!s_globalCamera.isInitialized())
    {
        if (!s_globalCamera.initialize())
        {
            return false;
        }
    }

    bool success = s_globalCamera.captureFrame(out);
    if (!success)
    {
        s_globalCamera.cleanup();
    }
    return success;
}

#else

#if defined(_WIN32)
#include <windows.h>
#include <vfw.h>
#pragma comment(lib, "vfw32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace
{
LRESULT CALLBACK videoFrameCallback(HWND window, LPVIDEOHDR header)
{
    Camera* camera = reinterpret_cast<Camera*>(
        capGetUserData(window));
    if (camera != NULL && header != NULL)
    {
        camera->onVideoFrame(header->lpData, 0, 0, 0);
    }
    return TRUE;
}
}
#endif

Camera::Camera()
    : m_pSourceReader(NULL)
    , m_width(0)
    , m_height(0)
    , m_stride(0)
    , m_formatFourCC(0)
    , m_isInitialized(false)
    , m_comInitialized(false)
    , m_mfStarted(false)
    , m_captureWindow(NULL)
    , m_frameWidth(0)
    , m_frameHeight(0)
    , m_frameReady(false)
{
}

Camera::~Camera()
{
    cleanup();
}

bool Camera::initialize()
{
#if defined(_WIN32)
    HWND window = capCreateCaptureWindowA(
        "BarcodeScannerCamera", WS_CHILD, 0, 0, 1, 1,
        GetDesktopWindow(), 0);
    if (window == NULL)
    {
        std::cerr << "[Camera] capCreateCaptureWindow failed.\n";
        return false;
    }

    m_captureWindow = window;
    capSetUserData(window, reinterpret_cast<LPARAM>(this));
    if (!capDriverConnect(window, 0))
    {
        std::cerr << "[Camera] capDriverConnect failed: no compatible webcam driver.\n";
        cleanup();
        return false;
    }

    if (!capSetCallbackOnFrame(window, videoFrameCallback))
    {
        std::cerr << "[Camera] capSetCallbackOnFrame failed.\n";
        cleanup();
        return false;
    }

    BITMAPINFOHEADER format;
    ZeroMemory(&format, sizeof(format));
    format.biSize = sizeof(format);
    if (!capGetVideoFormat(window, &format, sizeof(format)))
    {
        std::cerr << "[Camera] capGetVideoFormat failed.\n";
        cleanup();
        return false;
    }

    m_width = format.biWidth;
    m_height = format.biHeight < 0 ? -format.biHeight : format.biHeight;
    m_stride = m_width * 3;
    m_frameWidth = m_width;
    m_frameHeight = m_height;
    m_frameReady = false;
    m_isInitialized = true;
    std::cerr << "[Camera] Video-for-Windows camera initialized: "
              << m_width << "x" << m_height << ".\n";
    return true;
#else
    std::cerr << "[Camera] Media Foundation backend is not compiled because the "
                 "compiler could not find the Windows Media Foundation headers "
                 "(mfapi.h, mfidl.h, mfreadwrite.h, mferror.h).\n";
    return false;
#endif
}

void Camera::cleanup()
{
#if defined(_WIN32)
    HWND window = reinterpret_cast<HWND>(m_captureWindow);
    if (window != NULL)
    {
        capSetCallbackOnFrame(window, NULL);
        capDriverDisconnect(window);
        DestroyWindow(window);
    }
#endif
    m_pSourceReader = NULL;
    m_captureWindow = NULL;
    m_width = 0;
    m_height = 0;
    m_stride = 0;
    m_formatFourCC = 0;
    m_isInitialized = false;
    m_comInitialized = false;
    m_mfStarted = false;
    m_latestFrame.clear();
    m_frameWidth = 0;
    m_frameHeight = 0;
    m_frameReady = false;
}

bool Camera::isInitialized() const
{
    return m_isInitialized;
}

#if !defined(BARCODE_HAS_MEDIA_FOUNDATION)
void Camera::onVideoFrame(const unsigned char* data, int width, int height, int stride)
{
#if defined(_WIN32)
    if (data == NULL)
    {
        return;
    }
    if (width <= 0) width = m_width;
    if (height <= 0) height = m_height;
    if (stride <= 0) stride = ((width * 3) + 3) & ~3;

    m_latestFrame.resize(static_cast<size_t>(width * height));
    for (int y = 0; y < height; ++y)
    {
        const unsigned char* source = data + (height - 1 - y) * stride;
        for (int x = 0; x < width; ++x)
        {
            const unsigned char* pixel = source + x * 3;
            m_latestFrame[static_cast<size_t>(y * width + x)] =
                static_cast<unsigned char>(
                    (114 * pixel[0] + 587 * pixel[1] + 299 * pixel[2]) / 1000);
        }
    }
    m_frameWidth = width;
    m_frameHeight = height;
    m_frameReady = true;
#else
    (void)data;
    (void)width;
    (void)height;
    (void)stride;
#endif
}
#endif

unsigned char* Camera::readFrameRaw(int& outWidth, int& outHeight)
{
    outWidth = 0;
    outHeight = 0;
    return NULL;
}

bool Camera::captureFrame(Image& out)
{
#if defined(_WIN32)
    HWND window = reinterpret_cast<HWND>(m_captureWindow);
    if (!m_isInitialized || window == NULL)
    {
        return false;
    }
    m_frameReady = false;
    if (!capGrabFrameNoStop(window))
    {
        std::cerr << "[Camera] capGrabFrameNoStop failed.\n";
        return false;
    }
    if (!m_frameReady)
    {
        std::cerr << "[Camera] Webcam returned no video frame.\n";
        return false;
    }
    out.width = m_frameWidth;
    out.height = m_frameHeight;
    out.pixels = m_latestFrame;
    return true;
#else
    (void)out;
    return false;
#endif
}

bool Camera::captureFrame(Image* out)
{
    (void)out;
    return false;
}

Image* Camera::captureImage()
{
    return NULL;
}

unsigned char* Camera::captureRawGrayscale(int& width, int& height)
{
    width = 0;
    height = 0;
    return NULL;
}

bool Camera::captureSingleFrame(Image& out)
{
    (void)out;
    return false;
}

bool captureFrame(Image& out)
{
#if defined(_WIN32)
    static Camera camera;
    if (!camera.isInitialized() && !camera.initialize())
    {
        return false;
    }
    return camera.captureFrame(out);
#else
    (void)out;
    std::cerr << "[Camera] Media Foundation backend is unavailable; "
                 "camera capture cannot start.\n";
    return false;
#endif
}

#endif
