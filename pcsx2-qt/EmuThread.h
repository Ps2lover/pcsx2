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

#pragma once
#include "pcsx2/Host.h"
#include "pcsx2/HostDisplay.h"
#include <QtCore/QEventLoop>
#include <QtCore/QSemaphore>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <atomic>
#include <memory>

class DisplayWidget;
struct VMBootParameters;

class EmuThread : public QThread
{
  Q_OBJECT

public:
  EmuThread(QThread* ui_thread);
  ~EmuThread();

  static void start();
  static void stop();

  __fi QEventLoop* getEventLoop() const { return m_event_loop; }

  bool isOnEmuThread() const;

  /// Called back from the GS thread when the display state changes (e.g. fullscreen, render to main).
  HostDisplay* acquireHostDisplay(HostDisplay::RenderAPI api);
  void releaseHostDisplay();
  void updateDisplay();

  void startBackgroundControllerPollTimer();
  void stopBackgroundControllerPollTimer();

public Q_SLOTS:
  void startVM(std::shared_ptr<VMBootParameters> boot_params);
  void resetVM();
  void setVMPaused(bool paused);
  void shutdownVM(bool allow_save_to_state = true, bool blocking = false);
  void loadState(const QString& filename);
  void loadStateFromSlot(qint32 slot);
  void saveState(const QString& filename);
  void saveStateToSlot(qint32 slot);
  void toggleFullscreen();
  void setFullscreen(bool fullscreen);
  void applySettings();
  void toggleSoftwareRendering();
  void switchRenderer(GSRendererType renderer);
  void reloadPatches();
  void reloadInputSources();
  void reloadInputBindings();
  void requestDisplaySize(float scale);

Q_SIGNALS:
  DisplayWidget* onCreateDisplayRequested(bool fullscreen, bool render_to_main);
  DisplayWidget* onUpdateDisplayRequested(bool fullscreen, bool render_to_main);
  void onResizeDisplayRequested(qint32 width, qint32 height);
  void onDestroyDisplayRequested();
  void onVMStarting();
  void onVMStarted();
  void onVMPaused();
  void onVMResumed();
  void onVMStopped();
  void onGameChanged(const QString& path, const QString& serial, const QString& name, quint32 crc);

protected:
  void run();

private:
  static constexpr u32 BACKGROUND_CONTROLLER_POLLING_INTERVAL =
    100; /// Interval at which the controllers are polled when the system is not active.

  void connectDisplaySignals(DisplayWidget* widget);
  void destroyVM();
  void executeVM();
  void checkForSettingChanges();

  void createBackgroundControllerPollTimer();
  void destroyBackgroundControllerPollTimer();

private Q_SLOTS:
  void stopInThread();
  void doBackgroundControllerPoll();
  void onDisplayWindowMouseMoveEvent(int x, int y);
  void onDisplayWindowMouseButtonEvent(int button, bool pressed);
  void onDisplayWindowMouseWheelEvent(const QPoint& delta_angle);
  void onDisplayWindowResized(int width, int height, float scale);
  void onDisplayWindowFocused();
  void onDisplayWindowKeyEvent(int key, int mods, bool pressed);

private:
  QThread* m_ui_thread;
  QSemaphore m_started_semaphore;
  QEventLoop* m_event_loop = nullptr;
  QTimer* m_background_controller_polling_timer = nullptr;

  std::atomic_bool m_shutdown_flag{false};

  bool m_is_rendering_to_main = false;
  bool m_is_fullscreen = false;
};

extern EmuThread* g_emu_thread;