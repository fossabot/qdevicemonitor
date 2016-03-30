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
#include <QRegularExpression>

using namespace DataTypes;

static QStringList s_filesToOpen;

TextFileDevice::TextFileDevice(QPointer<QTabWidget> parent, const QString& id, const DeviceType type,
                               const QString& humanReadableName, const QString& humanReadableDescription, QPointer<DeviceAdapter> deviceAdapter)
    : BaseDevice(parent, id, type, humanReadableName, humanReadableDescription, deviceAdapter)
{
    qDebug() << "TextFileDevice::TextFileDevice";
    m_deviceWidget->getFilterLineEdit().setToolTip(tr("Search for messages. Accepts<ul><li>Plain Text</li><li>Prefix <b>text:</b> with Plain Text</li><li>Regular Expressions</li></ul>"));
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

    if (m_tailProcess.state() != QProcess::NotRunning)
    {
        m_tailProcess.terminate();
        m_tailProcess.kill();
        m_tailProcess.close();
    }
}

void TextFileDevice::update()
{
    switch (m_tailProcess.state())
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

            for (int i = 0; i < DeviceAdapter::MAX_LINES_UPDATE && m_tailProcess.canReadLine(); ++i)
            {
                m_tempStream << m_tailProcess.readLine();
                const QString line = m_tempStream.readLine();
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

void TextFileDevice::checkFilters(bool& filtersMatch, bool& filtersValid, const QStringRef& text)
{
    filtersValid = true;

    const QString textString(text.toString());

    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it)
    {
        const QStringRef filter(&(*it));
        if (!columnTextMatches(filter, textString))
        {
            filtersMatch = false;
            break;
        }
    }
}

void TextFileDevice::filterAndAddToTextEdit(const QString& line)
{
    bool filtersMatch = true;

    static const QRegularExpression re(
        "(?<prefix>[A-Za-z]* +[\\d]+ [\\d:]+) (?<hostname>.+) ",
        QRegularExpression::InvertedGreedinessOption | QRegularExpression::DotMatchesEverythingOption
    );

    const int themeIndex = m_deviceAdapter->isDarkTheme() ? 1 : 0;
    const QRegularExpressionMatch match = re.match(line);
    if (match.hasMatch())
    {
        const QStringRef prefix = match.capturedRef("prefix");
        const QStringRef hostname = match.capturedRef("hostname");
        const QStringRef text = line.midRef(match.capturedEnd("hostname") + 1);

        checkFilters(filtersMatch, m_filtersValid, text);

        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::DateTime], prefix);
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityWarn], hostname);
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityVerbose], text);
            m_deviceWidget->flushText();
        }
    }
    else
    {
        checkFilters(filtersMatch, m_filtersValid, QStringRef(&line));
        if (filtersMatch)
        {
            m_deviceWidget->addText(ThemeColors::Colors[themeIndex][ThemeColors::VerbosityVerbose], QStringRef(&line));
            m_deviceWidget->flushText();
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
    for (auto logFileIt = s_filesToOpen.constBegin(); logFileIt != s_filesToOpen.constEnd(); ++logFileIt)
    {
        const QString& logFile = *logFileIt;
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

void TextFileDevice::onLogReady()
{
    // TODO: implement
}
