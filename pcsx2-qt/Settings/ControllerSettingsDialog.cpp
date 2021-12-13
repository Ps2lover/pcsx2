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

#include "QtHost.h"
#include "Settings/ControllerSettingsDialog.h"
#include "Settings/ControllerGlobalSettingsWidget.h"
#include "Settings/ControllerBindingWidgets.h"
#include "Settings/HotkeySettingsWidget.h"

#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTextEdit>

ControllerSettingsDialog::ControllerSettingsDialog(QWidget* parent /* = nullptr */)
	: QDialog(parent)
{
	m_ui.setupUi(this);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	m_global_settings = new ControllerGlobalSettingsWidget(m_ui.settingsContainer, this);
	m_ui.settingsContainer->insertWidget(0, m_global_settings);

	for (u32 i = 0; i < MAX_PORTS; i++)
	{
		m_port_bindings[i] = new ControllerBindingWidget(m_ui.settingsContainer, i);
		m_ui.settingsContainer->insertWidget(i + 1, m_port_bindings[i]);
	}

	m_hotkey_settings = new HotkeySettingsWidget(m_ui.settingsContainer, this);
	m_ui.settingsContainer->insertWidget(3, m_hotkey_settings);

	m_ui.settingsCategory->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	m_ui.settingsCategory->setCurrentRow(1);
	m_ui.settingsContainer->setCurrentIndex(1);
	connect(m_ui.settingsCategory, &QListWidget::currentRowChanged, this, &ControllerSettingsDialog::onCategoryCurrentRowChanged);
	connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &ControllerSettingsDialog::close);
}

ControllerSettingsDialog::~ControllerSettingsDialog() = default;

void ControllerSettingsDialog::setCategory(Category category)
{
	switch (category)
	{
		case Category::GlobalSettings:
			m_ui.settingsContainer->setCurrentIndex(0);
			break;

			// TODO: These will need to take multitap into consideration in the future.
		case Category::FirstControllerSettings:
			m_ui.settingsContainer->setCurrentIndex(1);
			break;

		case Category::HotkeySettings:
			m_ui.settingsContainer->setCurrentIndex(3);
			break;

		default:
			break;
	}
}

void ControllerSettingsDialog::onCategoryCurrentRowChanged(int row)
{
	m_ui.settingsContainer->setCurrentIndex(row);
}

