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

#include "IOSDevice.h"
#include "Utils.h"
#include "ThemeColors.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

using namespace DataTypes;

static QSharedPointer<QString> s_tempBuffer;
static QSharedPointer<QTextStream> s_tempStream;

static QProcess s_devicesListProcess;
static QHash<QString, bool> s_removedDeviceByTabClose;

IOSDevice::IOSDevice(QPointer<QTabWidget> parent, const QString& id, const DeviceType type,
                     const QString& humanReadableName, const QString& humanReadableDescription, QPointer<DeviceAdapter> deviceAdapter)
    : BaseDevice(parent, id, type, humanReadableName, humanReadableDescription, deviceAdapter)
    , m_didReadModel(false)
{
    qDebug() << "IOSDevice::IOSDevice";

    m_tempErrorsStream.setCodec("UTF-8");
    m_tempErrorsStream.setString(&m_tempErrorsBuffer, QIODevice::ReadWrite | QIODevice::Text);

    m_deviceWidget->getFilterLineEdit().setToolTip(tr("Search for messages. Accepts<ul><li>Plain Text</li><li>Prefix <b>text:</b> with Plain Text</li><li>Regular Expressions</li></ul>"));
    m_deviceWidget->hideVerbosity();

    updateModel();
    connect(&m_logProcess, &QProcess::readyReadStandardOutput, this, &BaseDevice::logReady);
    connect(&m_infoProcess, &QProcess::readyReadStandardError, this, &BaseDevice::logReady);
}

IOSDevice::~IOSDevice()
{
    qDebug() << "IOSDevice::~IOSDevice";
    stopLogger();
    disconnect(&m_logProcess, &QProcess::readyReadStandardOutput, this, &BaseDevice::logReady);
    disconnect(&m_infoProcess, &QProcess::readyReadStandardError, this, &BaseDevice::logReady);
    stopInfoProcess();
}

void IOSDevice::stopInfoProcess()
{
    if (m_infoProcess.state() != QProcess::NotRunning)
    {
        m_infoProcess.terminate();
        m_infoProcess.kill();
        m_infoProcess.close();
    }
}

void IOSDevice::updateModel()
{
    qDebug() << "updateModel" << m_id;
    QStringList args;
    args.append("-u");
    args.append(m_id);
    args.append("-s");
    args.append("-k");
    args.append("DeviceName");
    m_infoProcess.setReadChannel(QProcess::StandardOutput);
    m_infoProcess.start("ideviceinfo", args);
}

void IOSDevice::startLogger()
{
    if (!m_didReadModel)
    {
        return;
    }

    qDebug() << "IOSDevice::startLogger";

    const QString currentLogAbsFileName = Utils::getNewLogFilePath(
        QString("%1-%2-")
            .arg(getPlatformString())
            .arg(Utils::removeSpecialCharacters(m_humanReadableName))
    );
    m_currentLogFileName = currentLogAbsFileName;
    m_deviceWidget->onLogFileNameChanged(m_currentLogFileName);

    m_logFile.setFileName(currentLogAbsFileName);
    m_logFile.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Truncate);
    m_logFileStream = QSharedPointer<QTextStream>::create(&m_logFile);
    m_logFileStream->setCodec("UTF-8");

    QStringList args;
    args.append("-u");
    args.append(m_id);
    m_logProcess.setReadChannel(QProcess::StandardOutput);
    m_logProcess.start("idevicesyslog", args);
}

void IOSDevice::stopLogger()
{
    qDebug() << "IOSDevice::stopLogger";

    if (m_logProcess.state() != QProcess::NotRunning)
    {
        m_logProcess.terminate();
        m_logProcess.kill();
        m_logProcess.close();
    }
    m_logFileStream.clear();
    m_logFile.close();
}

void IOSDevice::update()
{
    if (!m_didReadModel && m_infoProcess.state() == QProcess::NotRunning)
    {
        if (m_infoProcess.canReadLine())
        {
            const QString model = m_infoProcess.readLine().trimmed();
            if (!model.isEmpty())
            {
                qDebug() << "updateModel" << m_id << "=>" << model;
                m_humanReadableName = model;
                updateTabWidget();
                m_didReadModel = true;
                stopLogger();
                startLogger();
            }
        }

        stopInfoProcess();

        if (!m_didReadModel)
        {
            updateModel();
        }
    }

    switch (m_logProcess.state())
    {
    case QProcess::Running:
        {
            if (m_dirtyFilter)
            {
                m_dirtyFilter = false;
                const QString filter = m_deviceWidget->getFilterLineEdit().text();
                m_filters = filter.split(' ');
                m_filtersValid = true;
                reloadTextEdit();
                maybeAddCompletionAfterDelay(filter);
            }
        }
        break;
    case QProcess::NotRunning:
        {
            qDebug() << "m_logProcess not running";
            stopLogger();  // FIXME: remove?
            startLogger();
        }
        break;
    case QProcess::Starting:
    default:
        break;
    }
}

void IOSDevice::checkFilters(bool& filtersMatch, bool& filtersValid, const QStringRef& text)
{
    QString textString;
    bool textStringInitialized = false;

    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it)
    {
        const QStringRef filter(&(*it));
        bool columnFound = false;
        if (!columnMatches("text:", filter, text, filtersValid, columnFound))
        {
            filtersMatch = false;
            break;
        }

        if (!columnFound)
        {
            if (!textStringInitialized)
            {
                textStringInitialized = true;
                textString = text.toString();
            }

            if (!columnTextMatches(filter, textString))
            {
                filtersMatch = false;
                break;
            }
        }
    }
}

void IOSDevice::filterAndAddToTextEdit(const QString& line)
{
    if (line == QString("[connected]") || line == QString("[disconnected]"))
    {
        return;
    }

    static const QRegularExpression re(
        "(?<prefix>[A-Za-z]* +[\\d]+ [\\d:]+) (?<deviceName>.+) ",
        QRegularExpression::InvertedGreedinessOption | QRegularExpression::DotMatchesEverythingOption
    );

    const int themeIndex = m_deviceAdapter->isDarkTheme() ? 1 : 0;
    const QRegularExpressionMatch match = re.match(line);
    if (match.hasMatch())
    {
        const QStringRef prefix = match.capturedRef("prefix");
        const QStringRef deviceName = match.capturedRef("deviceName");
        const QStringRef text = line.midRef(match.capturedEnd("hostname") + 1);

        bool filtersMatch = true;
        checkFilters(filtersMatch, m_filtersValid, text);

        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::DateTime], prefix);
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityWarn], deviceName);
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityVerbose], text);
            m_deviceWidget->flushText();
        }
    }
    else
    {
        bool filtersMatch = true;
        checkFilters(filtersMatch, m_filtersValid, QStringRef(&line));

        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityInfo], QStringRef(&line));
            m_deviceWidget->flushText();
        }
    }

    m_deviceWidget->maybeScrollTextEditToEnd();
    m_deviceWidget->highlightFilterLineEdit(!m_filtersValid);
}

void IOSDevice::reloadTextEdit()
{
    if (!m_didReadModel)
    {
        return;
    }

    qDebug() << "reloadTextEdit";
    m_deviceWidget->clearTextEdit();

    updateLogBufferSpace();
    filterAndAddFromLogBufferToTextEdit();
}

void IOSDevice::maybeAddNewDevicesOfThisType(QPointer<QTabWidget> parent, DevicesMap& map, QPointer<DeviceAdapter> deviceAdapter)
{
    if (s_devicesListProcess.state() == QProcess::NotRunning)
    {
        if (s_tempStream.isNull())
        {
            s_tempStream = QSharedPointer<QTextStream>::create();
            s_tempBuffer = QSharedPointer<QString>::create();
            s_tempStream->setCodec("UTF-8");
            s_tempStream->setString(&(*s_tempBuffer), QIODevice::ReadWrite | QIODevice::Text);
        }

        bool deviceListError = false;

        if (s_devicesListProcess.exitCode() != 0 ||
            s_devicesListProcess.exitStatus() == QProcess::ExitStatus::CrashExit)
        {
            deviceListError = true;

            *s_tempStream << s_devicesListProcess.readAllStandardError();
            const QString errorText = s_tempStream->readLine();

            if (s_devicesListProcess.exitCode() != 0xFF || errorText != "ERROR: Unable to retrieve device list!")
            {
                qDebug() << "IOSDevice::s_devicesListProcess exitCode" << s_devicesListProcess.exitCode()
                         << "; exitStatus" << s_devicesListProcess.exitStatus()
                         << "; stderr" << errorText;
            }
            else
            {
#if defined(Q_OS_LINUX)
                // TODO: if ps uax | grep usbmuxd
                deviceListError = false;
#endif
            }
        }

        if (!deviceListError)
        {
            for (auto& i : s_removedDeviceByTabClose)
            {
                i = false;  // not visited
            }

            for (auto& dev : map)
            {
                if (dev->getType() == DeviceType::IOS)
                {
                    dev->setVisited(false);
                }
            }

            if (s_devicesListProcess.canReadLine())
            {
                *s_tempStream << s_devicesListProcess.readAll();

                QString deviceId;
                while (!s_tempStream->atEnd())
                {
                    const bool lineIsRead = s_tempStream->readLineInto(&deviceId);
                    if (!lineIsRead)
                    {
                        break;
                    }

                    //qDebug() << "deviceId" << deviceId;
                    if (s_removedDeviceByTabClose.contains(deviceId))
                    {
                        s_removedDeviceByTabClose[deviceId] = true;  // visited
                    }
                    else
                    {
                        auto it = map.find(deviceId);
                        if (it == map.end())
                        {
                            map[deviceId] = QSharedPointer<IOSDevice>::create(
                                parent,
                                deviceId,
                                DeviceType::IOS,
                                QString(getPlatformStringStatic()),
                                tr("Initializing..."),
                                deviceAdapter
                            );
                        }
                        else if ((*it)->getType() != DeviceType::IOS)
                        {
                            qDebug() << "id collision";
                        }
                        else
                        {
                            (*it)->updateInfo(true);
                        }
                    }
                }
            }

            for (auto& dev : map)
            {
                if (dev->getType() == DeviceType::IOS)
                {
                    if (!dev->isVisited())
                    {
                        if (!s_removedDeviceByTabClose.contains(dev->getId()))
                        {
                            dev->updateInfo(false);
                        }
                    }
                }
            }

            for (auto it = s_removedDeviceByTabClose.begin(); it != s_removedDeviceByTabClose.end(); )
            {
                const bool becameOffline = it.value() == false;
                if (becameOffline)
                {
                    it = s_removedDeviceByTabClose.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        stopDevicesListProcess();

        QStringList args;
        args.append("-l");
        s_devicesListProcess.setReadChannel(QProcess::StandardOutput);
        s_devicesListProcess.start("idevice_id", args);
    }
}

void IOSDevice::onLogReady()
{
    maybeReadErrorsPart();
    const bool allErrorsAreRead = m_tempErrorsStream.atEnd();

    if (allErrorsAreRead)
    {
        maybeReadLogPart();
    }

    if (m_logProcess.canReadLine() || !allErrorsAreRead)
    {
        emit logReady();
    }
}

void IOSDevice::maybeReadErrorsPart()
{
    m_tempErrorsStream << m_infoProcess.readAllStandardError();

    const int themeIndex = m_deviceAdapter->isDarkTheme() ? 1 : 0;
    for (int i = 0; i < DeviceAdapter::MAX_LINES_UPDATE && !m_tempErrorsStream.atEnd(); ++i)
    {
        const QString line = m_tempErrorsStream.readLine();
        m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityAssert], QStringRef(&line));
        m_deviceWidget->flushText();
    }
}

void IOSDevice::maybeReadLogPart()
{
    for (int i = 0; i < DeviceAdapter::MAX_LINES_UPDATE && m_logProcess.canReadLine(); ++i)
    {
        m_tempStream << m_logProcess.readLine();
        const QString line = m_tempStream.readLine();
        *m_logFileStream << line << "\n";
        m_logFileStream->flush();
        addToLogBuffer(line);
        filterAndAddToTextEdit(line);
    }
}

void IOSDevice::releaseTempBuffer()
{
    qDebug() << "IOSDevice::releaseTempBuffer";
    if (!s_tempStream.isNull())
    {
        s_tempStream.clear();
        s_tempBuffer.clear();
    }
}

void IOSDevice::stopDevicesListProcess()
{
    if (s_devicesListProcess.state() != QProcess::NotRunning)
    {
        s_devicesListProcess.terminate();
        s_devicesListProcess.kill();
        s_devicesListProcess.close();
    }
}

void IOSDevice::removedDeviceByTabClose(const QString& id)
{
    s_removedDeviceByTabClose[id] = false;
}
