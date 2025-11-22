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

#ifndef DECKLINKDISCOVERY_HPP
#define DECKLINKDISCOVERY_HPP

#include <QObject>
#include <QString>
#include <QList>
#include <QRect>

#ifdef Q_OS_WIN
#include <comdef.h>
#include <objbase.h>
#include <comutil.h>

// Forward declarations for DeckLink COM interfaces
struct IDeckLink;
struct IDeckLinkIterator;
struct IDeckLinkProfileAttributes;

// Note: These GUIDs and constants are defined in DeckLink SDK
// For compilation, we need to either:
// 1. Include generated headers from .idl files, or
// 2. Use #import directive, or  
// 3. Define them here (simpler for basic functionality)
// We'll use option 3 for now - these values are from DeckLink SDK
#endif

struct DeckLinkDeviceInfo
{
    QString modelName;
    QString displayName;
    int deviceIndex;
    bool supportsPlayback;
    QRect geometry; // Virtual geometry for display purposes
};

class DeckLinkDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit DeckLinkDiscovery(QObject *parent = nullptr);
    ~DeckLinkDiscovery();

    bool initialize();
    void shutdown();
    QList<DeckLinkDeviceInfo> getAvailableDevices();
    bool isInitialized() const { return m_initialized; }

signals:
    void deviceArrived(int deviceIndex);
    void deviceRemoved(int deviceIndex);

private:
#ifdef Q_OS_WIN
    bool initializeCOM();
    void shutdownCOM();
    void enumerateDevices();
    
    IDeckLinkIterator* m_deckLinkIterator;
    QList<DeckLinkDeviceInfo> m_devices;
    bool m_initialized;
    bool m_comInitialized;
    int m_baseScreenIndex; // Base index for DeckLink devices (after regular screens)
#endif
};

#endif // DECKLINKDISCOVERY_HPP

