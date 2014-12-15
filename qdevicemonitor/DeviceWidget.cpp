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

#include "DeviceWidget.h"
#include "DataTypes.h"

#include <QDebug>
#include <QPalette>
#include <QScrollBar>

using namespace DataTypes;

DeviceWidget::DeviceWidget(QPointer<QWidget> parent, QPointer<DeviceAdapter> deviceAdapter)
    : QWidget(parent)
    , ui(new Ui::DeviceWidget)
    , m_deviceAdapter(deviceAdapter)
{
    ui->setupUi(this);

    m_textStream.setCodec("UTF-8");
    m_textStream.setString(&m_stringStream, QIODevice::ReadWrite | QIODevice::Text);

    if (m_deviceAdapter->isDarkTheme())
    {
        QPalette pal;
        pal.setColor(QPalette::Text, Qt::white);
        pal.setColor(QPalette::Base, Qt::black);
        ui->textEdit->setPalette(pal);
    }

    ui->textEdit->setFontFamily(m_deviceAdapter->getFont());
    ui->textEdit->setFontPointSize(m_deviceAdapter->getFontSize());
    ui->textEdit->document()->setMaximumBlockCount(m_deviceAdapter->getVisibleBlocks());

    ui->verbositySlider->valueChanged(ui->verbositySlider->value());
    ui->wrapCheckBox->setCheckState(ui->wrapCheckBox->isChecked() ? Qt::Checked : Qt::Unchecked);
}

void DeviceWidget::on_verbositySlider_valueChanged(int value)
{
    qDebug() << "verbosity" << value;
    const char* const v = Verbosity[value];
    ui->verbosityLabel->setText(tr(v));
}

void DeviceWidget::on_wrapCheckBox_toggled(bool checked)
{
    ui->textEdit->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
    maybeScrollTextEditToEnd();
}

void DeviceWidget::on_scrollLockCheckBox_toggled(bool)
{
    maybeScrollTextEditToEnd();
}

int DeviceWidget::getVerbosityLevel() const
{
    return ui->verbositySlider->value();
}

void DeviceWidget::highlightFilterLineEdit(bool red)
{
    static QPalette normalPal = ui->filterLineEdit->palette();
    static QPalette redPal(Qt::red);
    redPal.setColor(QPalette::Highlight, Qt::red);

    ui->filterLineEdit->setPalette(red ? redPal : normalPal);
}

void DeviceWidget::maybeScrollTextEditToEnd()
{
    if (!ui->scrollLockCheckBox->isChecked())
    {
        scrollTextEditToEnd();
    }
}

void DeviceWidget::addText(const QColor& color, const QString& text)
{
    ui->textEdit->setTextColor(color);
    if (text.endsWith("\n"))
    {
        m_textStream << QString("<font color=\"%1\">%2</font>").arg(color.name()).arg(text.left(text.length() - 1));
        ui->textEdit->append(m_textStream.readLine());
    }
    else
    {
        m_textStream << QString("<font color=\"%1\">%2</font>").arg(color.name()).arg(text);
    }
}

void DeviceWidget::scrollTextEditToEnd()
{
    QScrollBar& sb = *(getTextEdit().verticalScrollBar());
    if (sb.maximum() > 0)
    {
        sb.setValue(sb.maximum());
    }
}
