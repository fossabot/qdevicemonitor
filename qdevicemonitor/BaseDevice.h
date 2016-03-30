/*
    This file is part of QDeviceMonitor.

    QDeviceMonitor is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    QDeviceMonitor is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with QDeviceMonitor. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BASEDEVICE_H
#define BASEDEVICE_H

#include "DeviceWidget.h"
#include "DeviceAdapter.h"
#include "DataTypes.h"

#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringRef>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

using namespace DataTypes;

class BaseDevice : public QObject
{
    Q_OBJECT

public:
    explicit BaseDevice(QPointer<QTabWidget> parent, const QString& id, const DeviceType type,
                        const QString& humanReadableName, const QString& humanReadableDescription,
                        QPointer<DeviceAdapter> deviceAdapter);
    ~BaseDevice() override;

    void updateTabWidget();
    virtual void update() = 0;
    virtual void filterAndAddToTextEdit(const QString& line) = 0;
    virtual const char* getPlatformString() const = 0;
    virtual void reloadTextEdit() = 0;

    void maybeAddCompletionAfterDelay(const QString& filter);

    inline const QString& getHumanReadableName() const { return m_humanReadableName; }
    inline const QString& getHumanReadableDescription() const { return m_humanReadableDescription; }

    inline void setHumanReadableName(const QString& text) { m_humanReadableName = text; }
    inline void setHumanReadableDescription(const QString& text) { m_humanReadableDescription = text; }

    inline bool isOnline() const { return m_online; }
    void setOnline(const bool online);
    virtual void onOnlineChange(const bool online) { (void) online; }

    inline void setVisited(const bool visited) { m_visited = visited; }
    inline bool isVisited() const { return m_visited; }

    inline int getTabIndex() const { return m_tabIndex; }
    inline void setTabIndex(const int tabIndex) { m_tabIndex = tabIndex; }

    inline const QString& getId() const { return m_id; }
    inline DeviceType getType() const { return m_type; }
    void updateInfo(const bool online, const QString& additional = QString());

    inline const QString& getCurrentLogFileName() const { return m_currentLogFileName; }

    void addToLogBuffer(const QString& text);
    void updateLogBufferSpace();
    void filterAndAddFromLogBufferToTextEdit();
    bool columnMatches(const QString& column, const QStringRef& filter, const QStringRef& originalValue, bool& filtersValid, bool& columnFound);
    bool columnTextMatches(const QStringRef& filter, const QString& text);

signals:
    void logReady();

private slots:
    void addFilterAsCompletion();
    void updateFilter(const QString& filter);
    virtual void onLogReady() {}; // FIXME: pure virtual?

protected:
    const QString m_id;
    DeviceType m_type;
    QString m_humanReadableName;
    QString m_humanReadableDescription;
    bool m_online;
    QPointer<QTabWidget> m_tabWidget;
    QSharedPointer<DeviceWidget> m_deviceWidget;
    int m_tabIndex;
    QPointer<DeviceAdapter> m_deviceAdapter;
    QString m_currentLogFileName;
    bool m_dirtyFilter;
    bool m_filtersValid;
    QStringList m_filters;
    QStringList m_logBuffer;
    QRegularExpression m_columnTextRegexp;
    QString m_tempBuffer;
    QTextStream m_tempStream;

private:
    bool m_visited;
    QString m_completionToAdd;
    QTimer m_completionAddTimer;
};

#endif // BASEDEVICE_H
