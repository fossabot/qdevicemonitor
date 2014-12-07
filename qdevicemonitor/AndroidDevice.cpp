#include "AndroidDevice.h"
#include "Utils.h"
#include "ThemeColors.h"

#include <QDebug>
#include <QRegExp>
#include <QRegExpValidator>
#include <QStringList>
#include <QWeakPointer>

using namespace DataTypes;

static QProcess s_devicesListProcess;
static const char* PLATFORM_STRING = "Android";

AndroidDevice::AndroidDevice(QPointer<QTabWidget> parent, const QString& id, DeviceType type,
                             const QString& humanReadableName, const QString& humanReadableDescription, QPointer<DeviceAdapter> deviceAdapter)
    : BaseDevice(parent, id, type, humanReadableName, humanReadableDescription, deviceAdapter)
    , m_emptyTextEdit(true)
    , m_lastVerbosityLevel(m_deviceWidget->getVerbosityLevel())
    , m_lastFilter(m_deviceWidget->getFilterLineEdit().text())
    , m_didReadModel(false)
{
    updateDeviceModel();
    m_reloadTextEditTimer.setSingleShot(true);
    connect(&m_reloadTextEditTimer, SIGNAL(timeout()), this, SLOT(reloadTextEdit()));
}

AndroidDevice::~AndroidDevice()
{
    qDebug() << "~AndroidDevice";
    disconnect(&m_reloadTextEditTimer, SIGNAL(timeout()));
    stopLogger();
    m_deviceInfoProcess.close();
}

void AndroidDevice::updateDeviceModel()
{
    qDebug() << "updateDeviceModel" << m_id;
    QStringList args;
    args.append("-s");
    args.append(m_id);
    args.append("shell");
    args.append("getprop");
    args.append("ro.product.model");
    m_deviceInfoProcess.start("adb", args);
    m_deviceInfoProcess.setReadChannel(QProcess::StandardOutput);
}

void AndroidDevice::startLogger()
{
    if (!m_didReadModel)
    {
        return;
    }

    m_deviceLogFile.setFileName(
        Utils::getNewLogFilePath("Android-" + Utils::removeSpecialCharacters(m_humanReadableName) + "-")
    );
    m_deviceLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    m_deviceLogFileStream = QSharedPointer<QTextStream>(new QTextStream(&m_deviceLogFile));
    m_deviceLogFileStream->setCodec("UTF-8");

    QStringList args;
    args.append("-s");
    args.append(m_id);
    args.append("logcat");
    args.append("-v");
    args.append("threadtime");
    args.append("*:v");
    m_deviceLogProcess.start("adb", args);
    m_deviceLogProcess.setReadChannel(QProcess::StandardOutput);
}

void AndroidDevice::stopLogger()
{
    m_deviceLogProcess.close();
    //m_deviceLogFileStream->flush();
    m_deviceLogFileStream.clear();
    m_deviceLogFile.close();
}

void AndroidDevice::update()
{
    if (!m_didReadModel && m_deviceInfoProcess.state() == QProcess::NotRunning)
    {
        if (m_deviceInfoProcess.canReadLine())
        {
            QString model = m_deviceInfoProcess.readLine().trimmed();
            if (!model.isEmpty())
            {
                qDebug() << "updateDeviceModel" << m_id << "=>" << model;
                m_humanReadableName = model;
                updateTabWidget();
                m_didReadModel = true;
                startLogger();
            }
        }

        m_deviceInfoProcess.close();

        if (!m_didReadModel)
        {
            updateDeviceModel();
        }
    }

    if (m_deviceLogProcess.state() == QProcess::Running)
    {
        const QString filter = m_deviceWidget->getFilterLineEdit().text();
        if (m_deviceWidget->getVerbosityLevel() != m_lastVerbosityLevel)
        {
            m_lastVerbosityLevel = m_deviceWidget->getVerbosityLevel();
            scheduleReloadTextEdit();
        }
        else if (m_lastFilter.compare(filter) != 0)
        {
            m_lastFilter = filter;
            scheduleReloadTextEdit();
        }
        else if(m_deviceLogProcess.canReadLine())
        {
            QString stringStream;
            QTextStream stream;
            stream.setCodec("UTF-8");
            stream.setString(&stringStream, QIODevice::ReadOnly | QIODevice::Text);
            //m_deviceWidget->getTextEdit().setPlainText(m_deviceLogProcess.readAll());
            stream << m_deviceLogProcess.readAll();

            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                *m_deviceLogFileStream << line << "\n";
                filterAndAddToTextEdit(line);
            }
        }
    }
}

void AndroidDevice::filterAndAddToTextEdit(const QString& line)
{
    static QRegExp rx("([\\d-]+) *([\\d:\\.]+) *(\\d+) *(\\d+) *([A-Z]) *(.+):", Qt::CaseSensitive, QRegExp::W3CXmlSchema11);
    rx.setMinimal(true);

    QStringList filters = m_deviceWidget->getFilterLineEdit().text().split(" ");
    bool filtersMatch = true;
    bool filtersValid = true;

    int theme = m_deviceAdapter->isDarkTheme() ? 1 : 0;
    if (rx.indexIn(line) > -1)
    {
        QString date = rx.cap(1);
        QString time = rx.cap(2);
        QString pid = rx.cap(3);
        QString tid = rx.cap(4);
        QString verbosity = rx.cap(5);
        QString tag = rx.cap(6).trimmed();
        QString text = line.mid(rx.pos(6) + rx.cap(6).length() + 2); // the rest of the line after "foo: "
        //qDebug() << "date" << date << "time" << time << "pid" << pid << "tid" << tid << "level" << verbosity << "tag" << tag << "text" << text;

        VerbosityEnum verbosityLevel = static_cast<VerbosityEnum>(Utils::verbosityCharacterToInt(verbosity.toStdString()[0])); // FIXME
        checkFilters(filtersMatch, filtersValid, filters, verbosityLevel, pid, tid, tag, text);

        if (filtersMatch)
        {
            QColor verbosityColor = ThemeColors::Colors[theme][verbosityLevel];

            m_deviceWidget->getTextEdit().setTextColor(verbosityColor);
            m_deviceWidget->getTextEdit().insertPlainText(verbosity + " ");

            m_deviceWidget->getTextEdit().setTextColor(QColor(ThemeColors::Colors[theme][ThemeColors::DateTime]));
            m_deviceWidget->getTextEdit().insertPlainText(date + " ");
            m_deviceWidget->getTextEdit().insertPlainText(time + " ");

            m_deviceWidget->getTextEdit().setTextColor(ThemeColors::Colors[theme][ThemeColors::Pid]);
            m_deviceWidget->getTextEdit().insertPlainText(pid + " ");
            m_deviceWidget->getTextEdit().setTextColor(ThemeColors::Colors[theme][ThemeColors::Tid]);
            m_deviceWidget->getTextEdit().insertPlainText(tid + " ");

            m_deviceWidget->getTextEdit().setTextColor(ThemeColors::Colors[theme][ThemeColors::Tag]);
            m_deviceWidget->getTextEdit().insertPlainText(tag + " ");

            m_deviceWidget->getTextEdit().setTextColor(verbosityColor);
            m_deviceWidget->getTextEdit().insertPlainText(text + "\n");
        }

        m_deviceWidget->maybeScrollTextEditToEnd();
    }
    else
    {
        qDebug() << "failed to parse" << line;
        checkFilters(filtersMatch, filtersValid, filters);
        if (filtersMatch)
        {
            m_deviceWidget->getTextEdit().setTextColor(ThemeColors::Colors[theme][ThemeColors::VerbosityVerbose]);
            m_deviceWidget->getTextEdit().insertPlainText(line + "\n");
        }
    }

    if (!filtersValid) // TODO
    {
        qDebug() << "filtersValid == false";
    }
}

bool AndroidDevice::columnMatches(const QString& column, const QString& filter, const QString& originalValue, bool& filtersValid) const
{
    if (filter.startsWith(column))
    {
        QString value = filter.mid(column.length());
        if (value.isEmpty())
        {
            filtersValid = false;
        }
        else if (!originalValue.startsWith(value))
        {
            return false;
        }
    }
    return true;
}

bool AndroidDevice::columnTextMatches(const QString& filter, const QString& text) const
{
    // TODO: ... or check regexp
    QString f = filter.trimmed();
    return f.isEmpty() || text.indexOf(f) != -1;
}

void AndroidDevice::checkFilters(bool& filtersMatch, bool& filtersValid, const QStringList& filters, VerbosityEnum verbosityLevel, const QString& pid, const QString& tid, const QString& tag, const QString& text) const
{
    filtersMatch = verbosityLevel <= m_deviceWidget->getVerbosityLevel();

    if (!filtersMatch)
    {
        return;
    }

    for (auto it = filters.begin(); it != filters.end(); ++it)
    {
        const QString& filter = *it;
        if (!columnMatches("pid:", filter, pid, filtersValid) ||
            !columnMatches("tid:", filter, tid, filtersValid) ||
            !columnMatches("tag:", filter, tag, filtersValid) ||
            !columnMatches("text:", filter, text, filtersValid) ||
            !columnTextMatches(filter, text))
        {
            filtersMatch = false;
            break;
        }
    }
}

void AndroidDevice::reloadTextEdit()
{
    qDebug() << "reloadTextEdit";
    stopLogger();
    m_deviceWidget->getTextEdit().clear();
    startLogger();
}

void AndroidDevice::scheduleReloadTextEdit(int timeout)
{
    m_reloadTextEditTimer.stop();
    m_reloadTextEditTimer.start(timeout);
}

void AndroidDevice::addNewDevicesOfThisType(QPointer<QTabWidget> parent, DevicesMap& map, QPointer<DeviceAdapter> deviceAdapter)
{
    if (s_devicesListProcess.state() == QProcess::NotRunning)
    {
        if (s_devicesListProcess.canReadLine())
        {
            QString stringStream;
            QTextStream stream;
            stream.setCodec("UTF-8");
            stream.setString(&stringStream, QIODevice::ReadOnly | QIODevice::Text);
            stream << s_devicesListProcess.readAll();
            while (!stream.atEnd())
            {
                QString line = stream.readLine();
                if (!line.contains("List of devices attached"))
                {
                    QStringList lineSplit = line.split("\t");
                    if (lineSplit.count() >= 2)
                    {
                        QString deviceId = lineSplit[0];
                        QString deviceStatus = lineSplit[1];
                        //qDebug() << "deviceId" << deviceId << "; deviceStatus" << deviceStatus;
                        auto it = map.find(deviceId);
                        if (it == map.end())
                        {
                            map[deviceId] = QSharedPointer<BaseDevice>(
                                new AndroidDevice(parent, deviceId, DeviceType::Android, tr(PLATFORM_STRING), tr("Initializing..."), deviceAdapter)
                            );
                        }
                        else
                        {
                            bool online = deviceStatus == "device";
                            (*it)->setOnline(online);
                            (*it)->setHumanReadableDescription(
                                tr("%1\nStatus: %2\nID: %3%4")
                                    .arg(tr(PLATFORM_STRING))
                                    .arg(online ? "Online" : "Offline")
                                    .arg(deviceId)
                                    .arg(!online ? "\n" + deviceStatus : "")
                            );
                        }
                    }
                }
            }
            s_devicesListProcess.close();
        }

        QStringList args;
        args.append("devices");
        s_devicesListProcess.start("adb", args);
        s_devicesListProcess.setReadChannel(QProcess::StandardOutput);
    }
}

void AndroidDevice::stopDevicesListProcess()
{
    s_devicesListProcess.close();
}
