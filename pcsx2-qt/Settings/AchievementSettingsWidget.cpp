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

#include "AchievementSettingsWidget.h"
#include "AchievementLoginDialog.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "SettingWidgetBinder.h"
#include "QtUtils.h"

#include "pcsx2/Frontend/Achievements.h"
#include "pcsx2/HostSettings.h"

#include "common/StringUtil.h"

#include <QtCore/QDateTime>
#include <QtWidgets/QMessageBox>

AchievementSettingsWidget::AchievementSettingsWidget(SettingsDialog* dialog, QWidget* parent)
	: QWidget(parent)
	, m_dialog(dialog)
{
	m_ui.setupUi(this);

	SettingsInterface* sif = dialog->getSettingsInterface();

	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.richPresence, "Achievements", "RichPresence", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.testMode, "Achievements", "TestMode", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.unofficialTestMode, "Achievements",
		"UnofficialTestMode", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(sif, m_ui.useFirstDiscFromPlaylist, "Achievements",
		"UseFirstDiscFromPlaylist", true);
	m_ui.enable->setChecked(m_dialog->getEffectiveBoolValue("Achievements", "Enabled", false));
	m_ui.challengeMode->setChecked(m_dialog->getEffectiveBoolValue("Achievements", "ChallengeMode", false));

	dialog->registerWidgetHelp(m_ui.enable, tr("Enable Achievements"), tr("Unchecked"),
		tr("When enabled and logged in, PCSX2 will scan for achievements on game load."));
	dialog->registerWidgetHelp(m_ui.testMode, tr("Enable Test Mode"), tr("Unchecked"),
		tr("When enabled, PCSX2 will assume all achievements are locked and not send any "
		   "unlock notifications to the server."));
	dialog->registerWidgetHelp(
		m_ui.unofficialTestMode, tr("Test Unofficial Achievements"), tr("Unchecked"),
		tr("When enabled, PCSX2 will list achievements from unofficial sets. Please note that these achievements are "
		   "not tracked by RetroAchievements, so they unlock every time."));
	dialog->registerWidgetHelp(
		m_ui.richPresence, tr("Enable Rich Presence"), tr("Unchecked"),
		tr("When enabled, rich presence information will be collected and sent to the server where supported."));
	dialog->registerWidgetHelp(
		m_ui.useFirstDiscFromPlaylist, tr("Use First Disc From Playlist"), tr("Unchecked"),
		tr(
			"When enabled, the first disc in a playlist will be used for achievements, regardless of which disc is active."));
	dialog->registerWidgetHelp(m_ui.challengeMode, tr("Enable Hardcore Mode"), tr("Unchecked"),
		tr("\"Challenge\" mode for achievements. Disables save state, cheats, and slowdown "
		   "functions, but you receive double the achievement points."));

	connect(m_ui.enable, &QCheckBox::toggled, this, &AchievementSettingsWidget::onEnableToggled);
	connect(m_ui.loginButton, &QPushButton::clicked, this, &AchievementSettingsWidget::onLoginLogoutPressed);
	connect(m_ui.viewProfile, &QPushButton::clicked, this, &AchievementSettingsWidget::onViewProfilePressed);
	connect(m_ui.challengeMode, &QCheckBox::toggled, this, &AchievementSettingsWidget::onChallengeModeToggled);
	connect(g_emu_thread, &EmuThread::onRetroAchievementsRefreshed, this, &AchievementSettingsWidget::onAchievementsRefreshed);

	// disable account options when using per game settings
	if (m_dialog->isPerGameSettings())
		m_ui.loginButton->setEnabled(false);

	updateEnableState();
	updateLoginState();

	// force a refresh of game info
	Host::RunOnCPUThread(Host::OnRetroAchievementsRefreshed);
}

AchievementSettingsWidget::~AchievementSettingsWidget() = default;

void AchievementSettingsWidget::updateEnableState()
{
	const bool enabled = m_dialog->getEffectiveBoolValue("Achievements", "Enabled", false);
	m_ui.testMode->setEnabled(enabled);
	m_ui.useFirstDiscFromPlaylist->setEnabled(enabled);
	m_ui.richPresence->setEnabled(enabled);
	m_ui.challengeMode->setEnabled(enabled);
}

void AchievementSettingsWidget::updateLoginState()
{
	const std::string username(Host::GetBaseStringSettingValue("Achievements", "Username"));
	const bool logged_in = !username.empty();

	if (logged_in)
	{
		const u64 login_unix_timestamp = StringUtil::FromChars<u64>(Host::GetBaseStringSettingValue("Achievements", "LoginTimestamp", "0")).value_or(0);
		const QDateTime login_timestamp(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(login_unix_timestamp)));
		m_ui.loginStatus->setText(tr("Username: %1\nLogin token generated on %2.")
									  .arg(QString::fromStdString(username))
									  .arg(login_timestamp.toString(Qt::TextDate)));
		m_ui.loginButton->setText(tr("Logout"));
	}
	else
	{
		m_ui.loginStatus->setText(tr("Not Logged In."));
		m_ui.loginButton->setText(tr("Login..."));
	}

	m_ui.viewProfile->setEnabled(logged_in);
}

void AchievementSettingsWidget::onLoginLogoutPressed()
{
	if (!Host::GetBaseStringSettingValue("Achievements", "Username").empty())
	{
		Host::RunOnCPUThread(Achievements::Logout, true);
		updateLoginState();
		return;
	}

	AchievementLoginDialog login(this);
	int res = login.exec();
	if (res != 0)
		return;

	updateLoginState();
}

void AchievementSettingsWidget::onViewProfilePressed()
{
	const std::string username(Host::GetBaseStringSettingValue("Achievements", "Username"));
	if (username.empty())
		return;

	const QByteArray encoded_username(QUrl::toPercentEncoding(QString::fromStdString(username)));
	QtUtils::OpenURL(
		QtUtils::GetRootWidget(this),
		QUrl(QStringLiteral("https://retroachievements.org/user/%1").arg(QString::fromUtf8(encoded_username))));
}

void AchievementSettingsWidget::onEnableToggled(bool checked)
{
	const bool challenge_mode = m_dialog->getEffectiveBoolValue("Achievements", "ChallengeMode", false);
	const bool challenge_mode_active = checked && challenge_mode;
	if (challenge_mode_active && !confirmChallengeModeEnable())
	{
		QSignalBlocker sb(m_ui.challengeMode);
		m_ui.challengeMode->setChecked(false);
		return;
	}

	m_dialog->setBoolSettingValue("Achievements", "Enabled", checked);

#if 0
	// FIXME
	if (challenge_mode)
		g_main_window->onAchievementsChallengeModeToggled(challenge_mode_active);
#endif

	updateEnableState();
}

void AchievementSettingsWidget::onChallengeModeToggled(bool checked)
{
	if (checked && !confirmChallengeModeEnable())
	{
		QSignalBlocker sb(m_ui.challengeMode);
		m_ui.challengeMode->setChecked(false);
		return;
	}

	m_dialog->setBoolSettingValue("Achievements", "ChallengeMode", checked);

#if 0
	// FIXME
	g_main_window->onAchievementsChallengeModeToggled(checked);
#endif
}

void AchievementSettingsWidget::onAchievementsRefreshed(quint32 id, const QString& game_info_string, quint32 total,
	quint32 points)
{
	m_ui.gameInfo->setText(game_info_string);
}

bool AchievementSettingsWidget::confirmChallengeModeEnable()
{
	if (!g_main_window->isVMValid())
		return true;

	QString message = tr("Enabling hardcore mode will shut down your current game.\n\n");

#if 0
	// FIXME
	if (ShouldSaveResumeState())
	{
		message +=
			tr("The current state will be saved, but you will be unable to load it until you disable hardcore mode.\n\n");
	}
#endif

	message += tr("Do you want to continue?");
	if (QMessageBox::question(QtUtils::GetRootWidget(this), tr("Enable Hardcore Mode"), message) != QMessageBox::Yes)
		return false;

	g_main_window->requestShutdown(false, true, false);
	return true;
}
