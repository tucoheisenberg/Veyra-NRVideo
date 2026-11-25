// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/KeyboardMouseCapture.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QSaveFile>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <format>

#include "veyra/Log.h"

namespace veyra::ui {
namespace {
std::int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
}

KeyboardMouseCapture::KeyboardMouseCapture(std::filesystem::path mappingFile, QObject* parent)
    : QObject(parent), mappingFile_(std::move(mappingFile)) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && captured_) capture(false);
    });
    mapper_.setConfig(remoteplay::kbm::parseConfig(remoteplay::kbm::defaultConfigText()).config);
}

KeyboardMouseCapture::~KeyboardMouseCapture() {
    capture(false);
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

bool KeyboardMouseCapture::writeDefaultMapping(const std::filesystem::path& file) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return true;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto defaults = remoteplay::kbm::defaultConfigText();
    // CRLF so that Notepad on older Windows shows the lines.
    QByteArray crlf(defaults.data(), qsizetype(defaults.size()));
    crlf.replace("\n", "\r\n");
    QSaveFile out(QString::fromStdWString(file.wstring()));
    if (!out.open(QIODevice::WriteOnly) || out.write(crlf) != crlf.size() || !out.commit()) {
        veyra::log::warn("remoteplay-kbm", "could not write the default mapping file");
        return false;
    }
    veyra::log::info("remoteplay-kbm", "default mapping file written");
    return true;
}

QStringList KeyboardMouseCapture::loadMapping() {
    QStringList problems;
    const auto defaults = remoteplay::kbm::defaultConfigText();
    QByteArray text(defaults.data(), qsizetype(defaults.size()));
    std::error_code ec;
    if (!std::filesystem::exists(mappingFile_, ec)) {
        if (!writeDefaultMapping(mappingFile_)) veyra::log::warn("remoteplay-kbm", "using built-in default mapping");
    } else {
        QFile file(QString::fromStdWString(mappingFile_.wstring()));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) problems << tr("键位文件无法读取，已使用默认键位");
        else text = file.readAll();
    }
    text.replace("\r", "");
    auto parsed = remoteplay::kbm::parseConfig(std::string_view(text.constData(), std::size_t(text.size())));
    for (const auto& w : parsed.warnings) {
        veyra::log::warn("remoteplay-kbm", "mapping " + w);
        problems << QString::fromStdString(w);
    }
    const bool wasDriving = driving();
    mapper_.setConfig(parsed.config);
    mapper_.setProfile(wasDriving ? remoteplay::kbm::Profile::Driving : remoteplay::kbm::Profile::OnFoot);
    return problems;
}

void KeyboardMouseCapture::setSensitivityPercent(int percent) { mapper_.setSensitivity(std::clamp(percent, 10, 400) / 100.0); }

bool KeyboardMouseCapture::capture(bool on) {
    if (on == captured_) return true;
    if (on) {
        if (!window_ || GetForegroundWindow() != window_) return false;
        RAWINPUTDEVICE device{0x01, 0x02, 0, window_};   // generic desktop / mouse
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            veyra::log::warn("remoteplay-kbm", "RegisterRawInputDevices failed; keyboard/mouse control not started");
            return false;
        }
        captured_ = true;
        mapper_.reset();
        swallowedUp_ = 0; wheelRemainder_[0] = wheelRemainder_[1] = 0;
        clipToWindow();
        SetCursor(nullptr);
        veyra::log::info("remoteplay-kbm", std::format("captured profile={}", driving() ? "driving" : "on-foot"));
    } else {
        captured_ = false;
        RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        mapper_.reset();   // the next merged state has nothing held
        veyra::log::info("remoteplay-kbm", "released");
    }
    emit capturedChanged(captured_);
    return true;
}

remoteplay::ControllerState KeyboardMouseCapture::merge(const remoteplay::ControllerState& pad) {
    if (!captured_) return pad;
    return mapper_.merge(pad, nowUs());
}

void KeyboardMouseCapture::clipToWindow() {
    RECT client{};
    if (!window_ || !GetClientRect(window_, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window_, &topLeft);
    ClientToScreen(window_, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool KeyboardMouseCapture::keyMessage(WPARAM wParam, LPARAM lParam, bool down) {
    const auto vk = std::uint32_t(wParam);
    if (down) {
        // Ctrl+Alt+Shift+Z: give the keyboard and mouse back (as for Moonlight).
        if (vk == 'Z' && keyDown(VK_CONTROL) && keyDown(VK_MENU) && keyDown(VK_SHIFT)) {
            swallowedUp_ = vk;
            capture(false);
            emit releaseRequested();
            return true;
        }
    } else if (swallowedUp_ == vk) {
        swallowedUp_ = 0;
        return true;
    }
    // Unbound function keys stay Veyra's own shortcuts.
    if (vk >= VK_F1 && vk <= VK_F12 && !mapper_.config().bound(std::uint16_t(vk))) return false;
    const bool repeat = down && (lParam & (1 << 30)) != 0;
    if (!repeat && mapper_.key(std::uint16_t(vk), down, nowUs())) {
        veyra::log::info("remoteplay-kbm", std::format("profile={}", driving() ? "driving" : "on-foot"));
        emit profileChanged(driving());
    }
    return true;
}

bool KeyboardMouseCapture::button(std::uint16_t code, bool down) {
    if (mapper_.key(code, down, nowUs())) emit profileChanged(driving());
    return true;
}

bool KeyboardMouseCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (!captured_ || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;

    bool consumed = false;
    switch (msg->message) {
    case WM_INPUT: {
        alignas(8) BYTE buffer[128];
        UINT size = sizeof(buffer);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) != UINT(-1)) {
            const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
            if (raw->header.dwType == RIM_TYPEMOUSE && (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
                mapper_.mouseMove(int(raw->data.mouse.lLastX), int(raw->data.mouse.lLastY), nowUs());
        }
        consumed = true;
        break;
    }
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE:
        clipToWindow();
        return false;   // the UI still needs the resize
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) capture(false);
        return false;
    case WM_KILLFOCUS:
        capture(false);
        return false;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: consumed = keyMessage(msg->wParam, msg->lParam, true); break;
    case WM_KEYUP: case WM_SYSKEYUP: consumed = keyMessage(msg->wParam, msg->lParam, false); break;
    case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: case WM_UNICHAR: case WM_IME_CHAR:
        consumed = true; break;
    case WM_SYSCOMMAND:
        consumed = (msg->wParam & 0xFFF0) == SC_KEYMENU;   // Alt must not open the window menu
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: consumed = button(0x01, true); break;
    case WM_LBUTTONUP: consumed = button(0x01, false); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: consumed = button(0x02, true); break;
    case WM_RBUTTONUP: consumed = button(0x02, false); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: consumed = button(0x04, true); break;
    case WM_MBUTTONUP: consumed = button(0x04, false); break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        consumed = button(GET_XBUTTON_WPARAM(msg->wParam) == XBUTTON1 ? 0x05 : 0x06, true); break;
    case WM_XBUTTONUP:
        consumed = button(GET_XBUTTON_WPARAM(msg->wParam) == XBUTTON1 ? 0x05 : 0x06, false); break;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        const bool horizontal = msg->message == WM_MOUSEHWHEEL;
        int& rest = wheelRemainder_[horizontal ? 1 : 0];
        rest += GET_WHEEL_DELTA_WPARAM(msg->wParam);
        const int notches = rest / WHEEL_DELTA;
        rest -= notches * WHEEL_DELTA;
        // Vertical: positive is away from the user (up). Horizontal: positive is right.
        if (notches > 0) mapper_.wheel(horizontal ? remoteplay::kbm::kWheelRight : remoteplay::kbm::kWheelUp, notches, nowUs());
        if (notches < 0) mapper_.wheel(horizontal ? remoteplay::kbm::kWheelLeft : remoteplay::kbm::kWheelDown, -notches, nowUs());
        consumed = true;
        break;
    }
    case WM_MOUSEMOVE:
        consumed = true; break;   // movement comes from raw input
    default:
        break;
    }
    if (consumed && result) *result = 0;
    return consumed;
}

} // namespace veyra::ui
