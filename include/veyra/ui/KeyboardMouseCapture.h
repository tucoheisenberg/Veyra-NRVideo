// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 键鼠控制 for PS5 手柄控制: while captured, the app window's keyboard and mouse
// messages become a virtual PS5 controller (remoteplay::kbm::Mapper) instead of
// reaching the UI. Same capture rules as the Moonlight keyboard/mouse capture:
// the native event filter sees the messages before Qt, the mouse is read as raw
// relative movement, the cursor is hidden and kept inside the window, and losing
// the foreground releases everything. Ctrl+Alt+Shift+Z releases the capture.
// F1-F12 that the mapping file leaves unbound still reach Veyra (F8 record, F9 replay).
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QStringList>
#include <windows.h>

#include <filesystem>

#include "veyra/remoteplay/KeyboardMouseMapper.h"

namespace veyra::ui {

class KeyboardMouseCapture final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit KeyboardMouseCapture(std::filesystem::path mappingFile, QObject* parent = nullptr);
    ~KeyboardMouseCapture() override;

    void setWindow(HWND topLevel) { window_ = topLevel; }
    bool captured() const { return captured_; }
    // Returns false when it cannot start (no window, or the window is not in the foreground).
    bool capture(bool on);

    // Reads the mapping file, writing the default one first when it does not exist.
    // Returns the problems found (the valid lines are used anyway).
    QStringList loadMapping();
    // Writes the default mapping file (CRLF, UTF-8) unless it already exists. False on a write error.
    static bool writeDefaultMapping(const std::filesystem::path& file);
    const std::filesystem::path& mappingFile() const { return mappingFile_; }
    void setSensitivityPercent(int percent);
    bool driving() const { return mapper_.profile() == remoteplay::kbm::Profile::Driving; }

    // The pad state with the keyboard and mouse merged in (the pad alone when not captured).
    remoteplay::ControllerState merge(const remoteplay::ControllerState& pad);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void capturedChanged(bool captured);
    void profileChanged(bool driving);
    void releaseRequested();   // Ctrl+Alt+Shift+Z (capture is already released)

private:
    void clipToWindow();
    bool keyMessage(WPARAM wParam, LPARAM lParam, bool down);
    bool button(std::uint16_t code, bool down);

    std::filesystem::path mappingFile_;
    remoteplay::kbm::Mapper mapper_;
    HWND window_ = nullptr;
    bool captured_ = false;
    std::uint32_t swallowedUp_ = 0;
    int wheelRemainder_[2] = {0, 0};
};

} // namespace veyra::ui
