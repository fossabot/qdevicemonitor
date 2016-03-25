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

#include "DeviceAdapter.h"
#include "SettingsDialog.h"
#include "Utils.h"

#include "AndroidDevice.h"
#include "IOSDevice.h"
#include "TextFileDevice.h"

#include <QDateTime>
#include <QDebug>
#include <QSet>

using namespace DataTypes;

DeviceAdapter::DeviceAdapter(QPointer<QTabWidget> parent)
    : QObject(parent)
    , m_visibleBlocks(500)
    , m_fontSize(12)
    , m_fontBold(false)
    , m_darkTheme(false)
    , m_clearAndroidLog(true)
    , m_autoRemoveFilesHours(48)
{
    qDebug() << "DeviceAdapter";
    m_filterCompleter.setModel(&m_filterCompleterModel);

    connect(&m_filesRemovalTimer, &QTimer::timeout, this, &DeviceAdapter::removeOldLogFiles);
    m_filesRemovalTimer.start(LOG_REMOVAL_FREQUENCY);
}

DeviceAdapter::~DeviceAdapter()
{
    qDebug() << "~DeviceAdapter";
    disconnect(&m_filesRemovalTimer, nullptr, this, nullptr);
}

void DeviceAdapter::start()
{
    qDebug() << "DeviceAdapter::start";
    update();
    connect(&m_updateTimer, &QTimer::timeout, this, &DeviceAdapter::update);
    m_updateTimer.start(UPDATE_FREQUENCY);
}

void DeviceAdapter::stop()
{
    qDebug() << "DeviceAdapter::stop";
    m_updateTimer.stop();
    disconnect(&m_updateTimer, &QTimer::timeout, this, &DeviceAdapter::update);

    AndroidDevice::stopDevicesListProcess();
    IOSDevice::stopDevicesListProcess();

    AndroidDevice::releaseTempBuffer();
    IOSDevice::releaseTempBuffer();
}

void DeviceAdapter::update()
{
    updateDevicesMap();
}

void DeviceAdapter::updateDevicesMap()
{
    for (int t = 0; t != static_cast<int>(DeviceType::DeviceTypeEnd); ++t)
    {
        const DeviceType type = static_cast<DeviceType>(t);

        switch (type)
        {
        case DeviceType::TextFile:
            TextFileDevice::maybeAddNewDevicesOfThisType(static_cast<QTabWidget*>(parent()), m_devicesMap, this);
            break;
        case DeviceType::Android:
            AndroidDevice::maybeAddNewDevicesOfThisType(static_cast<QTabWidget*>(parent()), m_devicesMap, this);
            break;
        case DeviceType::IOS:
            IOSDevice::maybeAddNewDevicesOfThisType(static_cast<QTabWidget*>(parent()), m_devicesMap, this);
            break;
        default:
            break;
        }
    }

    for (auto& device : m_devicesMap)
    {
        device->update();
    }
}

void DeviceAdapter::loadSettings(const QSettings& s)
{
    const QVariant visibleBlocks = s.value("visibleBlocks");
    if (visibleBlocks.isValid())
    {
        m_visibleBlocks = visibleBlocks.toInt();
    }

    const QVariant font = s.value("font");
    if (font.isValid())
    {
        m_font = font.toString();
    }
    else
    {
#if defined(Q_OS_MAC)
        m_font = "Monaco";
#elif defined(Q_OS_WIN32)
        m_font = "Lucida Console";
#else
        m_font = "monospace";
#endif
    }

    const QVariant fontSize = s.value("fontSize");
    if (fontSize.isValid())
    {
        m_fontSize = fontSize.toInt();
    }

    const QVariant fontBold = s.value("fontBold");
    if (fontBold.isValid())
    {
        m_fontBold = fontBold.toBool();
    }

    const QVariant darkTheme = s.value("darkTheme");
    if (darkTheme.isValid())
    {
        m_darkTheme = darkTheme.toBool();
    }

    const QVariant clearAndroidLog = s.value("clearAndroidLog");
    if (clearAndroidLog.isValid())
    {
        m_clearAndroidLog = clearAndroidLog.toBool();
    }

    const QVariant autoRemoveFilesHours = s.value("autoRemoveFilesHours");
    if (autoRemoveFilesHours.isValid())
    {
        m_autoRemoveFilesHours = autoRemoveFilesHours.toInt();
    }

    removeOldLogFiles();

    const QVariant textEditorPath = s.value("textEditorPath");
    if (textEditorPath.isValid())
    {
        m_textEditorPath = textEditorPath.toString();
    }

    if (m_textEditorPath.isEmpty())
    {
#if defined(Q_OS_MAC)
        m_textEditorPath = "/Applications/TextEdit.app/Contents/MacOS/TextEdit";
#elif defined(Q_OS_WIN32)
        m_textEditorPath = "notepad";
#else
        m_textEditorPath = "gedit";
#endif
    }

    const QVariant filterCompletions = s.value("filterCompletions");
    if (filterCompletions.isValid())
    {
        m_filterCompletions = filterCompletions.toStringList();
        m_filterCompleterModel.clear();
        for (const QString& filter : m_filterCompletions)
        {
            m_filterCompleterModel.appendRow(new QStandardItem(filter));
        }
    }

    const QVariant logFiles = s.value("logFiles");
    if (logFiles.isValid())
    {
        const QStringList list = logFiles.toStringList();
        for (const auto& i : list)
        {
            TextFileDevice::openLogFile(i);
        }
    }
}

void DeviceAdapter::saveSettings(QSettings& s)
{
    qDebug() << "DeviceAdapter::saveSettings";
    s.setValue("visibleBlocks", m_visibleBlocks);
    s.setValue("font", m_font);
    s.setValue("fontSize", m_fontSize);
    s.setValue("fontBold", m_fontBold);
    s.setValue("darkTheme", m_darkTheme);
    s.setValue("clearAndroidLog", m_clearAndroidLog);
    s.setValue("autoRemoveFilesHours", m_autoRemoveFilesHours);
    s.setValue("textEditorPath", m_textEditorPath);
    s.setValue("filterCompletions", m_filterCompletions);

    QStringList logFiles;
    for (const auto& device : m_devicesMap)
    {
        if (device->getType() == DeviceType::TextFile)
        {
            logFiles.append(device->getId());
        }
    }
    s.setValue("logFiles", logFiles);
}

void DeviceAdapter::addFilterAsCompletion(const QString& completionToAdd)
{
    qDebug() << "addFilterAsCompletion" << completionToAdd;
    if (m_filterCompletions.contains(completionToAdd))
    {
        qDebug() << completionToAdd << "is already in the list";
        return;
    }

    m_filterCompleterModel.appendRow(new QStandardItem(completionToAdd));
    m_filterCompletions.append(completionToAdd);

    const size_t oldCompletionsNumber =
        m_filterCompletions.size() > MAX_FILTER_COMPLETIONS
        ? m_filterCompletions.size() - MAX_FILTER_COMPLETIONS
        : 0;
    if (oldCompletionsNumber > 0)
    {
        qDebug() << "removing old" << oldCompletionsNumber << "completions";
        m_filterCompletions.erase(m_filterCompletions.begin(), m_filterCompletions.begin() + oldCompletionsNumber);
        m_filterCompleterModel.removeRows(0, int(oldCompletionsNumber));
    }
}

void DeviceAdapter::removeOldLogFiles()
{
    qDebug() << "removeOldLogFiles older than" << m_autoRemoveFilesHours << "hours (" << m_autoRemoveFilesHours * 60 * 60 << "seconds )";
    QStringList nameFilters;
    nameFilters.append(QString("*%1").arg(Utils::LOG_EXT));

    static const size_t dateLength = QString(Utils::DATE_FORMAT).length();
    static const size_t logExtLength = QString(Utils::LOG_EXT).length();

    QSet<QString> currentLogFileNames;
    for (const auto& device : m_devicesMap)
    {
        currentLogFileNames.insert(device->getCurrentLogFileName());
    }

    const QDateTime currentDateTime = QDateTime::currentDateTimeUtc();
    const QFileInfoList& list = QDir(Utils::getLogsPath()).entryInfoList(nameFilters, QDir::Files);
    for (const auto& fileInfo : list)
    {
        const QString& fileName = fileInfo.fileName();
        const size_t dateFileNameOffset = fileName.length() - dateLength - logExtLength;
        const QString dateString = fileName.mid(dateFileNameOffset, dateLength);
        const QDateTime dateTime = QDateTime::fromString(dateString, Utils::DATE_FORMAT).toUTC();

        const int dt = dateTime.secsTo(currentDateTime);
        const int autoRemoveFilesSeconds = m_autoRemoveFilesHours * 60 * 60;
        const bool oldFile = dt > autoRemoveFilesSeconds;
        const bool emptyFile = fileInfo.size() == 0ULL;
        const bool currentLogFile = currentLogFileNames.contains(fileName);
        const bool shouldRemove = (oldFile || emptyFile) && !currentLogFile;
        if (shouldRemove)
        {
            const bool result = QDir(Utils::getLogsPath()).remove(fileName);
            qDebug() << "removing" << fileName << "=>" << result;
        }
    }
}

void DeviceAdapter::allDevicesReloadText()
{
    for (auto& device : m_devicesMap)
    {
        device->reloadTextEdit();
    }
}

void DeviceAdapter::removeDeviceByTabIndex(const int index)
{
    qDebug() << "removeDeviceByTabIndex" << index;

    bool success = false;
    for (auto it = m_devicesMap.begin(); !success && it != m_devicesMap.end(); ++it)
    {
        qDebug() << "tabIndex" << it.value()->getTabIndex();
        if (it.value()->getTabIndex() == index)
        {
            const QPointer<QTabWidget> tabWidget = dynamic_cast<QTabWidget*>(parent());
            tabWidget->removeTab(index);
            if (it.value()->isOnline())
            {
                switch (it.value()->getType())
                {
                case DeviceType::Android:
                    AndroidDevice::removedDeviceByTabClose(it.key());
                    break;
                case DeviceType::IOS:
                    IOSDevice::removedDeviceByTabClose(it.key());
                    break;
                case DeviceType::TextFile:
                default:
                    break;
                }
            }

            m_devicesMap.remove(it.key());
            fixTabIndexes(index);
            success = true;
            break;
        }
    }

    Q_ASSERT_X(success, "removeDeviceByTabIndex", "tab is not found");
}

void DeviceAdapter::fixTabIndexes(const int removedTabIndex)
{
    for (auto it = m_devicesMap.begin(); it != m_devicesMap.end(); ++it)
    {
        const int tabIndex = it.value()->getTabIndex();
        Q_ASSERT_X(tabIndex != removedTabIndex, "fixTabIndexes", "tab removal failed");
        if (tabIndex > removedTabIndex)
        {
            qDebug() << "decrementing tabIndex" << tabIndex;
            it.value()->setTabIndex(tabIndex - 1);
        }
    }
}

void DeviceAdapter::focusFilterInput()
{
    qDebug() << "focusFilterInput";
    getCurrentDeviceWidget()->focusFilterInput();
}

void DeviceAdapter::markLog()
{
    qDebug() << "markLog";
    getCurrentDeviceWidget()->markLog();
}

void DeviceAdapter::clearLog()
{
    qDebug() << "clearLog";
    getCurrentDeviceWidget()->clearLog();
}

void DeviceAdapter::openLogFile()
{
    qDebug() << "openLogFile";
    getCurrentDeviceWidget()->openLogFile();
}

QPointer<DeviceWidget> DeviceAdapter::getCurrentDeviceWidget()
{
    QPointer<QTabWidget> tabWidget = dynamic_cast<QTabWidget*>(parent());
    QPointer<DeviceWidget> deviceWidget = dynamic_cast<DeviceWidget*>(tabWidget->currentWidget());
    return deviceWidget;
}
