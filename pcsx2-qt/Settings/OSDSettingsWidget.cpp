/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2022  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsDialog.h"
#include "OSDSettingsWidget.h"
#include <QtWidgets/QMessageBox>

OSDSettingsWidget::OSDSettingsWidget(QWidget* parent, SettingsDialog* dialog) : QWidget(parent)
{
  m_ui.setupUi(this);

  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.osdScale, "EmuCore/GS", "OsdScale", 100.0f);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowMessages, "EmuCore/GS", "OsdShowMessages", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowSpeed, "EmuCore/GS", "OsdShowSpeed", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowFPS, "EmuCore/GS", "OsdShowFPS", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowCPU, "EmuCore/GS", "OsdShowCPU", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowResolution, "EmuCore/GS", "OsdShowResolution", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.osdShowGSStats, "EmuCore/GS", "OsdShowGSStats", false);

  dialog->registerWidgetHelp(m_ui.osdShowMessages, tr("Show OSD Messages"), tr("Checked"),
                             tr("Shows on-screen-display messages when events occur such as save states being "
                                "created/loaded, screenshots being taken, etc."));
  dialog->registerWidgetHelp(m_ui.osdShowFPS, tr("Show Game Frame Rate"), tr("Unchecked"),
                             tr("Shows the internal frame rate of the game in the top-right corner of the display."));
  dialog->registerWidgetHelp(
    m_ui.osdShowSpeed, tr("Show Emulation Speed"), tr("Unchecked"),
    tr("Shows the current emulation speed of the system in the top-right corner of the display as a percentage."));
  dialog->registerWidgetHelp(m_ui.osdShowResolution, tr("Show Resolution"), tr("Unchecked"),
                             tr("Shows the resolution of the game in the top-right corner of the display."));
}

OSDSettingsWidget::~OSDSettingsWidget() = default;

