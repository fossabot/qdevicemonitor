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

#include "TextFileDevice.h"
#include "Utils.h"
#include "ThemeColors.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QRegExp>
#include <QtCore/QStringBuilder>

using namespace DataTypes;

static QStringList s_filesToOpen;

TextFileDevice::TextFileDevice(QPointer<QTabWidget> parent, const QString& id, const DeviceType type,
                               const QString& humanReadableName, const QString& humanReadableDescription, QPointer<DeviceAdapter> deviceAdapter)
    : BaseDevice(parent, id, type, humanReadableName, humanReadableDescription, deviceAdapter)
{
    qDebug() << "TextFileDevice::TextFileDevice";
    m_deviceWidget->getFilterLineEdit().setToolTip(tr("Search for messages. Accepts regexes and wildcards. Prefix with text: to limit scope."));
    m_deviceWidget->hideVerbosity();
    m_deviceWidget->onLogFileNameChanged(id);

    startLogger();
}

TextFileDevice::~TextFileDevice()
{
    qDebug() << "TextFileDevice::~TextFileDevice";
    stopLogger();
}

void TextFileDevice::startLogger()
{
    qDebug() << "TextFileDevice::startLogger";

    QStringList args;
    args.append("-F");
    args.append("-n");
    args.append(QString("%1").arg(m_deviceAdapter->getVisibleLines()));
    args.append(m_id);
    m_tailProcess.start("tail", args);
}

void TextFileDevice::stopLogger()
{
    qDebug() << "TextFileDevice::stopLogger";

    m_tailProcess.terminate();
    m_tailProcess.close();
}

void TextFileDevice::update()
{
    switch (m_tailProcess.state())
    {
    case QProcess::Running:
        {
            const QString filter = m_deviceWidget->getFilterLineEdit().text();
            if (m_lastFilter != filter)
            {
                m_filters = filter.split(" ");
                m_filtersValid = true;
                m_lastFilter = filter;
                reloadTextEdit();
                maybeAddCompletionAfterDelay(filter);
            }

            QString stringStream;
            QTextStream stream;
            stream.setCodec("UTF-8");
            stream.setString(&stringStream, QIODevice::ReadWrite | QIODevice::Text);

            for (int i = 0; i < DeviceAdapter::MAX_LINES_UPDATE && m_tailProcess.canReadLine(); ++i)
            {
                stream << m_tailProcess.readLine();
                const QString line = stream.readLine();
                addToLogBuffer(line);
                filterAndAddToTextEdit(line);
            }
        }
    case QProcess::NotRunning:
    case QProcess::Starting:
    default:
        break;
    }
}

void TextFileDevice::checkFilters(bool& filtersMatch, bool& filtersValid, const QStringList& filters, const QStringRef& text) const
{
    filtersValid = true;

    for (const auto& filter : filters)
    {
        if (!Utils::columnTextMatches(filter, text))
        {
            filtersMatch = false;
            break;
        }
    }
}

void TextFileDevice::filterAndAddToTextEdit(const QString& line)
{
    bool filtersMatch = true;

    QRegExp rx("([A-Za-z]* +[\\d]+ [\\d:]+) (.+) ", Qt::CaseSensitive, QRegExp::RegExp2);
    rx.setMinimal(true);

    const int themeIndex = m_deviceAdapter->isDarkTheme() ? 1 : 0;
    if (rx.indexIn(line) > -1)
    {
        const QString prefix = rx.cap(1);
        const QString hostname = rx.cap(2);
        const QStringRef text = line.midRef(rx.pos(2) + rx.cap(2).length() + 1);

        /*qDebug() << "prefix" << prefix;
        qDebug() << "hostname" << hostname;
        qDebug() << "text" << text;*/

        checkFilters(filtersMatch, m_filtersValid, m_filters, text);

        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::DateTime], prefix % " ");
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityWarn], hostname % " ");
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityVerbose], text % "\n");
        }
    }
    else
    {
        checkFilters(filtersMatch, m_filtersValid, m_filters, QStringRef(&line));
        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityVerbose], line % "\n");
        }
    }

    m_deviceWidget->maybeScrollTextEditToEnd();
    //m_deviceWidget->highlightFilterLineEdit(!m_filtersValid);
}

void TextFileDevice::reloadTextEdit()
{
    qDebug() << "reloadTextEdit";
    m_deviceWidget->clearTextEdit();

    updateLogBufferSpace();
    filterAndAddFromLogBufferToTextEdit();
}

void TextFileDevice::maybeAddNewDevicesOfThisType(QPointer<QTabWidget> parent, DevicesMap& map, QPointer<DeviceAdapter> deviceAdapter)
{
    for (const auto& logFile : s_filesToOpen)
    {
        const auto it = map.find(logFile);
        if (it == map.end())
        {
            const QString fileName = QFileInfo(logFile).fileName();
            map[logFile] = QSharedPointer<TextFileDevice>::create(
                parent,
                logFile,
                DeviceType::TextFile,
                fileName,
                logFile,
                deviceAdapter
            );
        }
    }

    s_filesToOpen.clear();
}

void TextFileDevice::openLogFile(const QString& logFile)
{
    s_filesToOpen.append(logFile);
}
