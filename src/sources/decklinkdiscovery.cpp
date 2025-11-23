/***************************************************************************
//
//    softProjector - an open source media projection software
//    Copyright (C) 2017  Vladislav Kobzar
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation version 3 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
***************************************************************************/

#include "../headers/decklinkdiscovery.hpp"
#include <QApplication>
#include <QScreen>
#include <QDebug>

#ifdef Q_OS_WIN
#include <comdef.h>
#include <objbase.h>
#include <comutil.h>
#include <shlwapi.h>

// Attribute ID type (must be defined before use)
typedef int64_t BMDDeckLinkAttributeID;

// DeckLink COM interface GUIDs (from DeckLinkAPI.idl)
// These GUIDs are from Blackmagic DeckLink SDK 15.2
// CLSID_CDeckLinkIterator: {DDF701E1-6216-40D8-9E70-E55CE97C0E0C}
static const GUID CLSID_CDeckLinkIterator = {0xDDF701E1, 0x6216, 0x40D8, {0x9E, 0x70, 0xE5, 0x5C, 0xE9, 0x7C, 0x0E, 0x0C}};
// IID_IDeckLinkIterator: {50FB36CD-3063-4B73-BDBB-958087F2D8BA} (from Mac/Linux headers)
// But Windows uses: {50C36AEF-3A05-4A7A-8101-888B0C0E0C0E} (from examples)
// Let's try the Windows version first
static const GUID IID_IDeckLinkIterator = {0x50C36AEF, 0x3A05, 0x4A7A, {0x81, 0x01, 0x88, 0x8B, 0x0C, 0x0E, 0x0C, 0x0E}};
// Alternative IID from Mac/Linux (try if first fails)
static const GUID IID_IDeckLinkIterator_Alt = {0x50FB36CD, 0x3063, 0x4B73, {0xBD, 0xBB, 0x95, 0x80, 0x87, 0xF2, 0xD8, 0xBA}};
static const GUID IID_IDeckLink = {0xC418FBDD, 0x0587, 0x48ED, {0x8F, 0xE5, 0x64, 0x0F, 0x0A, 0x14, 0xAF, 0x91}};
static const GUID IID_IDeckLinkProfileAttributes = {0x2B54EDEF, 0x5B32, 0x429F, {0xBA, 0x11, 0xEB, 0xAF, 0x8B, 0x0C, 0x0E, 0x0C}};

// DeckLink attribute constants (from DeckLinkAPITypes.idl)
#define BMDDeckLinkVideoIOSupport 0x6F747469 // 'otti'
#define BMDDeckLinkDuplex 0x64757870 // 'duxp'
#define bmdDeviceSupportsPlayback 0x00000001
#define bmdDuplexInactive 0x696E6163 // 'inac'

// Forward declarations for COM interfaces
struct IDeckLink : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetModelName(BSTR *modelName) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName(BSTR *displayName) = 0;
};

struct IDeckLinkIterator : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Next(IDeckLink **deckLinkInstance) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(void) = 0;
};

struct IDeckLinkProfileAttributes : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInt(BMDDeckLinkAttributeID cfgID, int64_t *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFlag(BMDDeckLinkAttributeID cfgID, BOOL *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(BMDDeckLinkAttributeID cfgID, BSTR *value) = 0;
};
#endif

DeckLinkDiscovery::DeckLinkDiscovery(QObject *parent)
    : QObject(parent)
#ifdef Q_OS_WIN
    , m_deckLinkIterator(nullptr)
    , m_initialized(false)
    , m_comInitialized(false)
    , m_baseScreenIndex(0)
#endif
{
}

DeckLinkDiscovery::~DeckLinkDiscovery()
{
    shutdown();
}

bool DeckLinkDiscovery::initialize()
{
#ifdef Q_OS_WIN
    if (m_initialized)
        return true;

    // Try to initialize COM, but don't fail if it's already initialized
    if (!initializeCOM())
    {
        // COM initialization failed, but this might be OK if it's already initialized by another thread
        // Continue anyway and try to create iterator
    }

    // Get the DeckLink iterator using COM
    // This will fail gracefully if DeckLink drivers are not installed
    qDebug() << "Attempting to create DeckLink iterator...";
    qDebug() << "CLSID_CDeckLinkIterator: DDF701E1-6216-40D8-9E70-E55CE97C0E0C";
    qDebug() << "IID_IDeckLinkIterator: 50C36AEF-3A05-4A7A-8101-888B0C0E0C0E";
    
    HRESULT result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&m_deckLinkIterator);
    
    // If first attempt fails, try alternative IID (some SDK versions use different GUID)
    if (result != S_OK)
    {
        qDebug() << "First attempt failed with error: 0x" << QString::number(result, 16).toUpper();
        qDebug() << "Trying alternative IID...";
        result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator_Alt, (void**)&m_deckLinkIterator);
    }
    
    if (result != S_OK)
    {
        // DeckLink drivers are not installed or device is not available
        // This is not an error - just means no DeckLink devices are available
        qDebug() << "DeckLink iterator creation failed. Error code: 0x" << QString::number(result, 16).toUpper();
        if (result == REGDB_E_CLASSNOTREG)
        {
            qDebug() << "Error: Class not registered. DeckLink drivers may not be installed.";
        }
        else if (result == E_NOINTERFACE)
        {
            qDebug() << "Error: Interface not supported. Driver version mismatch?";
        }
        else if (result == CO_E_NOTINITIALIZED)
        {
            qDebug() << "Error: COM not initialized properly.";
        }
        qDebug() << "This is OK if DeckLink hardware is not installed or drivers are not available.";
        
        // Clean up COM if we initialized it
        if (m_comInitialized)
        {
            shutdownCOM();
        }
        
        // Return false but don't treat this as a fatal error
        m_initialized = false;
        m_devices.clear();
        return false;
    }
    
    qDebug() << "DeckLink iterator created successfully!";

    // Successfully created iterator, now enumerate devices
    try {
        enumerateDevices();
        m_initialized = true;
        return true;
    }
    catch (...)
    {
        // If enumeration fails, clean up and continue without DeckLink
        qDebug() << "Error enumerating DeckLink devices, continuing without DeckLink support";
        if (m_deckLinkIterator)
        {
            m_deckLinkIterator->Release();
            m_deckLinkIterator = nullptr;
        }
        if (m_comInitialized)
        {
            shutdownCOM();
        }
        m_initialized = false;
        m_devices.clear();
        return false;
    }
#else
    // DeckLink SDK is Windows-only
    return false;
#endif
}

void DeckLinkDiscovery::shutdown()
{
#ifdef Q_OS_WIN
    try {
        if (m_deckLinkIterator)
        {
            m_deckLinkIterator->Release();
            m_deckLinkIterator = nullptr;
        }
    }
    catch (...)
    {
        // Ignore errors during shutdown
    }

    m_devices.clear();
    m_initialized = false;

    if (m_comInitialized)
    {
        shutdownCOM();
    }
#endif
}

QList<DeckLinkDeviceInfo> DeckLinkDiscovery::getAvailableDevices()
{
#ifdef Q_OS_WIN
    if (!m_initialized)
        initialize();

    return m_devices;
#else
    return QList<DeckLinkDeviceInfo>();
#endif
}

#ifdef Q_OS_WIN
bool DeckLinkDiscovery::initializeCOM()
{
    if (m_comInitialized)
        return true;

    HRESULT result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(result))
    {
        // RPC_E_CHANGED_MODE means COM was already initialized with different mode
        // This is OK, we can still use COM
        if (result == RPC_E_CHANGED_MODE)
        {
            // COM is already initialized, just mark it
            m_comInitialized = true;
            return true;
        }
        
        // Other errors - log but don't fail completely
        qDebug() << "COM initialization warning (may still work):" << result;
        // Don't set m_comInitialized to true, but don't return false either
        // Let the caller try to use COM anyway
        return true;
    }

    m_comInitialized = true;
    return true;
}

void DeckLinkDiscovery::shutdownCOM()
{
    if (m_comInitialized)
    {
        try {
            CoUninitialize();
        }
        catch (...)
        {
            // Ignore errors during COM uninitialization
        }
        m_comInitialized = false;
    }
}

void DeckLinkDiscovery::enumerateDevices()
{
    if (!m_deckLinkIterator)
        return;

    m_devices.clear();

    // Safety check - if iterator is invalid, return empty list
    if (!m_deckLinkIterator)
    {
        return;
    }

    // Get base screen index (number of regular screens)
    int regularScreenCount = QApplication::screens().count();
    m_baseScreenIndex = regularScreenCount;

    // Calculate virtual geometry for DeckLink devices
    // We'll place them after regular screens
    int virtualX = 0;
    int virtualY = 0;
    
    // Find the rightmost screen position
    for (QScreen* screen : QApplication::screens())
    {
        QRect screenGeometry = screen->geometry();
        int rightEdge = screenGeometry.x() + screenGeometry.width();
        if (rightEdge > virtualX)
            virtualX = rightEdge;
    }

    IDeckLink* deckLink = nullptr;
    int deviceIndex = 0;

    // Enumerate all DeckLink devices
    // Use try-catch to handle any COM errors gracefully
    qDebug() << "Starting DeckLink device enumeration...";
    int totalDevicesFound = 0;
    
    try {
        while (m_deckLinkIterator && m_deckLinkIterator->Next(&deckLink) == S_OK)
        {
            if (!deckLink)
            {
                qDebug() << "Warning: Received null DeckLink device pointer";
                continue;
            }

            totalDevicesFound++;
            DeckLinkDeviceInfo deviceInfo;
            deviceInfo.deviceIndex = m_baseScreenIndex + deviceIndex;
            deviceInfo.supportsPlayback = false; // Default value

            // Get model name
            BSTR modelNameBSTR = nullptr;
            HRESULT nameResult = deckLink->GetModelName(&modelNameBSTR);
            if (nameResult == S_OK && modelNameBSTR)
            {
                deviceInfo.modelName = QString::fromWCharArray(modelNameBSTR);
                SysFreeString(modelNameBSTR);
                qDebug() << "Found DeckLink device:" << deviceInfo.modelName;
            }
            else
            {
                deviceInfo.modelName = QString("DeckLink Device %1").arg(deviceIndex + 1);
                qDebug() << "Found DeckLink device (no model name), using default name:" << deviceInfo.modelName;
            }

            // Get display name
            BSTR displayNameBSTR = nullptr;
            HRESULT displayResult = deckLink->GetDisplayName(&displayNameBSTR);
            if (displayResult == S_OK && displayNameBSTR)
            {
                deviceInfo.displayName = QString::fromWCharArray(displayNameBSTR);
                SysFreeString(displayNameBSTR);
            }
            else
            {
                deviceInfo.displayName = deviceInfo.modelName;
            }

            // Check device attributes to determine capabilities
            IUnknown* unknown = nullptr;
            HRESULT attrResult = deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&unknown);
            if (attrResult == S_OK && unknown)
            {
                IDeckLinkProfileAttributes* attributes = (IDeckLinkProfileAttributes*)unknown;
                if (attributes)
                {
                    // Check duplex mode first
                    int64_t duplexMode = 0;
                    int64_t duplexAttr = 0x64757870; // BMDDeckLinkDuplex
                    if (attributes->GetInt(duplexAttr, &duplexMode) == S_OK)
                    {
                        qDebug() << "  Duplex mode:" << duplexMode;
                        // If device is inactive, we still add it but note it
                        if (duplexMode == 0x696E6163) // bmdDuplexInactive
                        {
                            qDebug() << "  Warning: Device is in inactive duplex mode";
                        }
                    }
                    
                    // Check video I/O support
                    int64_t videoIOSupport = 0;
                    int64_t videoIOSupportAttr = 0x6F747469; // BMDDeckLinkVideoIOSupport
                    if (attributes->GetInt(videoIOSupportAttr, &videoIOSupport) == S_OK)
                    {
                        deviceInfo.supportsPlayback = (videoIOSupport & 0x00000001) != 0; // bmdDeviceSupportsPlayback
                        qDebug() << "  Video I/O Support: 0x" << QString::number(videoIOSupport, 16).toUpper();
                        qDebug() << "  Supports Playback:" << deviceInfo.supportsPlayback;
                    }
                    else
                    {
                        qDebug() << "  Warning: Could not get video I/O support, assuming playback supported";
                        // Assume playback is supported if we can't determine
                        deviceInfo.supportsPlayback = true;
                    }
                    
                    attributes->Release();
                }
            }
            else
            {
                qDebug() << "  Warning: Could not get device attributes, assuming playback supported";
                // If we can't get attributes, assume device supports playback
                deviceInfo.supportsPlayback = true;
            }

            // Set virtual geometry (1920x1080 default, positioned after regular screens)
            deviceInfo.geometry = QRect(virtualX + deviceIndex * 100, virtualY, 1920, 1080);

            // Add device to list - don't filter by supportsPlayback, show all devices
            m_devices.append(deviceInfo);

            qDebug() << "Added DeckLink device to list:" << deviceInfo.modelName 
                     << "Display:" << deviceInfo.displayName 
                     << "Index:" << deviceInfo.deviceIndex 
                     << "Supports Playback:" << deviceInfo.supportsPlayback;

            deckLink->Release();
            deckLink = nullptr;
            deviceIndex++;
        }
        
        qDebug() << "DeckLink enumeration complete. Total devices found:" << totalDevicesFound << "Added to list:" << m_devices.count();

        // Reset iterator for next enumeration (if still valid)
        if (m_deckLinkIterator)
        {
            m_deckLinkIterator->Reset();
        }
    }
    catch (...)
    {
        // If any error occurs during enumeration, just return empty list
        qDebug() << "Error during DeckLink device enumeration, continuing without DeckLink devices";
        m_devices.clear();
    }
}
#endif

