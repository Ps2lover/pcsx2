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

#include "DisplaySettingsWidget.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsDialog.h"
#include <QtWidgets/QMessageBox>

static constexpr int DEFAULT_INTERLACE_MODE = 7;

DisplaySettingsWidget::DisplaySettingsWidget(QWidget* parent, SettingsDialog* dialog) : QWidget(parent)
{
  m_ui.setupUi(this);

  SettingWidgetBinder::BindWidgetToEnumSetting(m_ui.aspectRatio, "EmuCore/GS", "AspectRatio",
                                               Pcsx2Config::GSOptions::AspectRatioNames, AspectRatioType::R4_3);
  SettingWidgetBinder::BindWidgetToEnumSetting(m_ui.fmvAspectRatio, "EmuCore/GS", "FMVAspectRatioSwitch",
                                               Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames,
                                               FMVAspectRatioSwitchType::Off);
  SettingWidgetBinder::BindWidgetToIntSetting(m_ui.interlacing, "EmuCore/GS", "interlace", DEFAULT_INTERLACE_MODE);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.bilinearFiltering, "EmuCore/GS", "LinearPresent", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.integerScaling, "EmuCore/GS", "IntegerScaling", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.internalResolutionScreenshots, "EmuCore/GS",
                                               "InternalResolutionScreenshots", false);
  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.zoom, "EmuCore/GS", "Zoom", 100.0f);
  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.stretchY, "EmuCore/GS", "StretchY", 100.0f);
  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.offsetX, "EmuCore/GS", "OffsetX", 0.0f);
  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.offsetY, "EmuCore/GS", "OffsetY", 0.0f);

  connect(m_ui.integerScaling, &QCheckBox::stateChanged, this, &DisplaySettingsWidget::onIntegerScalingChanged);

  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.startFullscreen, "UI", "StartFullscreen", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.doubleClickTogglesFullscreen, "UI", "DoubleClickTogglesFullscreen",
                                               true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.hideMouseCursor, "UI", "HideMouseCursor", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.renderToMainWindow, "UI", "RenderToMainWindow", true);

  connect(m_ui.fullscreenModes, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &DisplaySettingsWidget::onFullscreenModeChanged);

  dialog->registerWidgetHelp(m_ui.startFullscreen, tr("Start Fullscreen"), tr("Unchecked"),
                             tr("Automatically switches to fullscreen mode when a game is started."));
  dialog->registerWidgetHelp(m_ui.hideMouseCursor, tr("Hide Cursor In Fullscreen"), tr("Checked"),
                             tr("Hides the mouse pointer/cursor when the emulator is in fullscreen mode."));
  dialog->registerWidgetHelp(
    m_ui.renderToMainWindow, tr("Render To Main Window"), tr("Checked"),
    tr("Renders the display of the simulated console to the main window of the application, over "
       "the game list. If unchecked, the display will render in a separate window."));

  onIntegerScalingChanged();
}

DisplaySettingsWidget::~DisplaySettingsWidget() = default;

void DisplaySettingsWidget::onFullscreenModesChanged(const QStringList& modes)
{
  QSignalBlocker sb(m_ui.fullscreenModes);

  const QString current_mode(
    QString::fromStdString(QtHost::GetBaseStringSettingValue("EmuCore/GS", "FullscreenMode", "")));
  m_ui.fullscreenModes->clear();
  m_ui.fullscreenModes->addItem(tr("Borderless Fullscreen"));
  if (current_mode.isEmpty())
    m_ui.fullscreenModes->setCurrentIndex(0);

  for (const QString& mode : modes)
  {
    m_ui.fullscreenModes->addItem(mode);
    if (current_mode == mode)
      m_ui.fullscreenModes->setCurrentIndex(m_ui.fullscreenModes->count() - 1);
  }
}

void DisplaySettingsWidget::onIntegerScalingChanged()
{
  m_ui.bilinearFiltering->setEnabled(!m_ui.integerScaling->isChecked());
}

void DisplaySettingsWidget::onFullscreenModeChanged(int index)
{
  if (index == 0)
  {
    QtHost::RemoveBaseSettingValue("EmuCore/GS", "FullscreenMode");
  }
  else
  {
    QtHost::SetBaseStringSettingValue("EmuCore/GS", "FullscreenMode",
                                      m_ui.fullscreenModes->currentText().toUtf8().constData());
  }

  g_emu_thread->applySettings();
}