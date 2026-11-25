// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/KeyboardMouseMapper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace veyra::remoteplay::kbm {
namespace {
using B = ControllerState;

struct KeyName { const char* name; std::uint16_t code; };
// Names used in the mapping file. Arrow keys are ArrowUp... so that "Up" stays the D-pad action.
constexpr KeyName kKeys[] = {
    {"MouseLeft",0x01},{"MouseRight",0x02},{"MouseMiddle",0x04},{"MouseX1",0x05},{"MouseX2",0x06},
    {"Backspace",0x08},{"Tab",0x09},{"Enter",0x0D},{"Shift",0x10},{"Ctrl",0x11},{"Alt",0x12},
    {"Pause",0x13},{"CapsLock",0x14},{"Esc",0x1B},{"Escape",0x1B},{"Space",0x20},
    {"PageUp",0x21},{"PageDown",0x22},{"End",0x23},{"Home",0x24},
    {"ArrowLeft",0x25},{"ArrowUp",0x26},{"ArrowRight",0x27},{"ArrowDown",0x28},
    {"Insert",0x2D},{"Delete",0x2E},
    {"Semicolon",0xBA},{"Equals",0xBB},{"Comma",0xBC},{"Minus",0xBD},{"Period",0xBE},{"Slash",0xBF},
    {"Grave",0xC0},{"Backquote",0xC0},{"LeftBracket",0xDB},{"Backslash",0xDC},{"RightBracket",0xDD},{"Quote",0xDE},
    {"WheelUp",kWheelUp},{"WheelDown",kWheelDown},{"WheelLeft",kWheelLeft},{"WheelRight",kWheelRight},
};
struct ActionName { const char* name; Binding binding; };
constexpr ActionName kActions[] = {
    {"Cross",{Action::Button,B::Cross}},
    {"Circle",{Action::Button,B::Circle}},{"Square",{Action::Button,B::Square}},{"Triangle",{Action::Button,B::Triangle}},
    {"L1",{Action::Button,B::L1}},{"R1",{Action::Button,B::R1}},{"L3",{Action::Button,B::L3}},{"R3",{Action::Button,B::R3}},
    {"Up",{Action::Button,B::Up}},{"Down",{Action::Button,B::Down}},{"Left",{Action::Button,B::Left}},{"Right",{Action::Button,B::Right}},
    {"DpadUp",{Action::Button,B::Up}},{"DpadDown",{Action::Button,B::Down}},{"DpadLeft",{Action::Button,B::Left}},{"DpadRight",{Action::Button,B::Right}},
    {"Options",{Action::Button,B::Options}},{"Share",{Action::Button,B::Share}},{"Create",{Action::Button,B::Share}},
    {"Touchpad",{Action::Button,B::Touchpad}},{"PS",{Action::Button,B::PS}},
    {"L2",{Action::L2,0}},{"R2",{Action::R2,0}},
    {"LeftStickUp",{Action::LeftUp,0}},{"LeftStickDown",{Action::LeftDown,0}},{"LeftStickLeft",{Action::LeftLeft,0}},{"LeftStickRight",{Action::LeftRight,0}},
    {"RightStickUp",{Action::RightUp,0}},{"RightStickDown",{Action::RightDown,0}},{"RightStickLeft",{Action::RightLeft,0}},{"RightStickRight",{Action::RightRight,0}},
    {"Walk",{Action::Walk,0}},{"ToggleProfile",{Action::ToggleProfile,0}},{"OnFoot",{Action::OnFoot,0}},{"Driving",{Action::Driving,0}},
};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
bool actionForName(std::string_view name, Binding& out) {
    for (const auto& a : kActions) if (iequals(a.name, name)) { out = a.binding; return true; }
    return false;
}
bool number(std::string_view text, double& out) {
    const std::string s(trim(text));
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = v; return true;
}
std::int16_t axis(double v) {
    return std::int16_t(std::lround(std::clamp(v, -1.0, 1.0) * (v < 0 ? 32768.0 : 32767.0)));
}
double magnitude(std::int16_t x, std::int16_t y) { return std::hypot(double(x), double(y)); }

constexpr std::string_view kDefault = R"INI(; Veyra 键鼠控制 · 键位文件（PS5 手柄控制 / 采集卡）
; 修改后保存，下次点击画面开始键鼠控制时生效。删除本文件可恢复默认。
; 写法：按键 = 动作[, 动作...]   一个按键最多 3 个动作；同一动作可以绑多个按键。
; 默认参照 GTA 系列 PS 手柄布局。GTA6 若有不同，请在游戏「设置 → 控制」里核对后改这里。
;
; 按键名：A-Z 0-9 F1-F24 Space Enter Esc Tab Backspace CapsLock Shift Ctrl Alt
;   ArrowUp/Down/Left/Right Insert Delete Home End PageUp PageDown Numpad0-9
;   Grave(`) Minus Equals LeftBracket RightBracket Backslash Semicolon Quote Comma Period Slash
;   MouseLeft MouseRight MouseMiddle MouseX1 MouseX2 WheelUp WheelDown WheelLeft WheelRight
; 动作名：Cross Circle Square Triangle L1 R1 L2 R2 L3 R3 Up Down Left Right(十字键)
;   Options Share Touchpad PS
;   LeftStickUp/Down/Left/Right RightStickUp/Down/Left/Right（鼠标总是控制右摇杆）
;   Walk（按住时左摇杆只推一半：慢走）  ToggleProfile（步行⇄驾驶） OnFoot Driving
; 未绑定的 F1-F12 仍交给 Veyra（例如 F8 录制、F9 即时回放）。
; 释放键鼠：Ctrl+Alt+Shift+Z，或切到别的窗口。

[General]
WalkScale = 0.45
; 驾驶时 A/D 打满方向所需毫秒（模拟逐渐打方向，0 为立即打满）
SteerRampMs = 140
SteerReturnMs = 80
WheelPulseMs = 50

[Mouse]
; 灵敏度在 Veyra 采集卡面板里调。
; 反死区：游戏会忽略摇杆最开始的一段行程，鼠标稍动就至少推这么多（0-0.5）
Deadzone = 0.15
; 曲线指数：小于 1 小幅移动更灵敏，大于 1 更细腻
Curve = 1.0
; 每毫秒多少鼠标计数推满摇杆（灵敏度 100% 时）。DPI 高就调大
FullSpeed = 4.0
YRatio = 1.0
InvertY = 0
; 测速窗口（毫秒）：越小越跟手，越大越平滑；鼠标停下后摇杆在这个时间内回中
WindowMs = 12

[OnFoot]
W = LeftStickUp
S = LeftStickDown
A = LeftStickLeft
D = LeftStickRight
Alt = Walk
MouseLeft = R2
MouseRight = L2
MouseMiddle = R3
Shift = Cross
Space = Square
R = Circle
E = Circle
F = Triangle
Q = R1
Tab = L1
Ctrl = L3
C = R3
1 = Up
2 = Right
3 = Down
4 = Left
ArrowUp = Up
ArrowDown = Down
ArrowLeft = Left
ArrowRight = Right
WheelUp = Up
WheelDown = Down
M = Touchpad
Esc = Options
P = Options
Backspace = Share
Grave = ToggleProfile
; 上车同时切驾驶键位：把上一行 F 改成  F = Triangle, Driving

[Driving]
W = R2
S = L2
A = LeftStickLeft
D = LeftStickRight
ArrowUp = LeftStickUp
ArrowDown = LeftStickDown
Space = R1
MouseLeft = L1
MouseRight = L2
E = L3
H = L3
C = R3
F = Triangle
Shift = Cross
R = Circle
X = Square
Q = Left
Tab = Right
1 = Up
2 = Right
3 = Down
4 = Left
WheelUp = Up
WheelDown = Down
M = Touchpad
Esc = Options
P = Options
Backspace = Share
Grave = ToggleProfile
; 下车同时切回步行：F = Triangle, OnFoot
)INI";
} // namespace

std::string_view defaultConfigText() { return kDefault; }

std::uint16_t codeForName(std::string_view name) {
    name = trim(name);
    if (name.empty()) return 0;
    for (const auto& k : kKeys) if (iequals(k.name, name)) return k.code;
    if (name.size() == 1) {
        const char c = char(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return std::uint16_t(c);
    }
    auto suffix = [&](std::string_view prefix, int lo, int hi, std::uint16_t base) -> std::uint16_t {
        if (name.size() <= prefix.size() || !iequals(name.substr(0, prefix.size()), prefix)) return 0;
        int n = 0;
        for (char c : name.substr(prefix.size())) { if (c < '0' || c > '9') return 0; n = n * 10 + (c - '0'); if (n > 99) return 0; }
        return n >= lo && n <= hi ? std::uint16_t(base + n - lo) : 0;
    };
    if (auto c = suffix("Numpad", 0, 9, 0x60)) return c;
    if (auto c = suffix("F", 1, 24, 0x70)) return c;
    return 0;
}

std::string nameForCode(std::uint16_t code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, char(code));
    if (code >= 0x60 && code <= 0x69) return "Numpad" + std::to_string(code - 0x60);
    if (code >= 0x70 && code <= 0x87) return "F" + std::to_string(code - 0x70 + 1);
    for (const auto& k : kKeys) if (k.code == code) return k.name;
    return {};
}

bool Config::bound(std::uint16_t code) const {
    if (code >= kCodeCount) return false;
    for (const auto& profile : bindings)
        for (const auto& b : profile[code]) if (b.action != Action::None) return true;
    return false;
}

ParseResult parseConfig(std::string_view text) {
    ParseResult out;
    auto& c = out.config;
    enum class Section { None, General, Mouse, OnFoot, Driving, Unknown } section = Section::None;
    std::size_t lineNo = 0;
    while (!text.empty()) {
        const auto eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        ++lineNo;
        if (lineNo == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) line.remove_prefix(3);
        if (const auto comment = line.find_first_of(";#"); comment != std::string_view::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        auto warn = [&](const std::string& what) { out.warnings.push_back("line " + std::to_string(lineNo) + ": " + what); };
        if (line.front() == '[') {
            const auto name = trim(line.substr(1, line.find(']') == std::string_view::npos ? line.size() - 1 : line.find(']') - 1));
            section = iequals(name, "General") ? Section::General : iequals(name, "Mouse") ? Section::Mouse
                    : iequals(name, "OnFoot") || name == "步行" ? Section::OnFoot
                    : iequals(name, "Driving") || name == "驾驶" ? Section::Driving : Section::Unknown;
            if (section == Section::Unknown) warn("unknown section [" + std::string(name) + "]");
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) { warn("expected key = value"); continue; }
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        if (section == Section::General || section == Section::Mouse) {
            double v = 0;
            if (!number(value, v)) { warn("not a number: " + std::string(value)); continue; }
            auto set = [&](double& field, double lo, double hi) { field = std::clamp(v, lo, hi); };
            if (section == Section::General) {
                if (iequals(key, "WalkScale")) set(c.walkScale, 0.05, 1.0);
                else if (iequals(key, "SteerRampMs")) set(c.steerRampMs, 0, 2000);
                else if (iequals(key, "SteerReturnMs")) set(c.steerReturnMs, 0, 2000);
                else if (iequals(key, "WheelPulseMs")) set(c.wheelPulseMs, 16, 500);
                else warn("unknown setting " + std::string(key));
            } else {
                auto& m = c.mouse;
                if (iequals(key, "Deadzone")) set(m.deadzone, 0, 0.5);
                else if (iequals(key, "Curve")) set(m.exponent, 0.3, 3.0);
                else if (iequals(key, "FullSpeed")) set(m.fullSpeed, 0.2, 100);
                else if (iequals(key, "YRatio")) set(m.yRatio, 0.1, 5);
                else if (iequals(key, "InvertY")) m.invertY = v != 0;
                else if (iequals(key, "WindowMs")) set(m.windowMs, 4, 100);
                else warn("unknown setting " + std::string(key));
            }
            continue;
        }
        if (section != Section::OnFoot && section != Section::Driving) { warn("binding outside [OnFoot]/[Driving]"); continue; }
        const auto code = codeForName(key);
        if (!code) { warn("unknown key " + std::string(key)); continue; }
        auto& slot = c.bindings[section == Section::Driving ? 1 : 0][code];
        slot = {};
        std::size_t used = 0;
        std::string_view rest = value;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = trim(rest.substr(0, comma));
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            if (item.empty() || iequals(item, "None")) continue;
            Binding b;
            if (!actionForName(item, b)) { warn("unknown action " + std::string(item)); continue; }
            if (used == slot.size()) { warn("more than 3 actions for " + std::string(key)); break; }
            slot[used++] = b;
        }
    }
    return out;
}

void Mapper::setConfig(const Config& config) {
    const double s = config_.mouse.sensitivity;
    config_ = config;
    config_.mouse.sensitivity = s;
    reset();
}
void Mapper::setSensitivity(double s) { config_.mouse.sensitivity = std::clamp(s, 0.05, 10.0); }

void Mapper::reset() {
    down_.reset(); suppressed_.reset(); pulses_.clear(); pulseCode_ = 0; moves_.clear(); steer_ = 0; lastUs_ = 0;
}

bool Mapper::key(std::uint16_t code, bool down, std::int64_t) {
    if (code == 0 || code >= kCodeCount) return false;
    if (suppressed_[code]) { if (!down) suppressed_[code] = false; return false; }
    const bool was = down_[code];
    down_[code] = down;
    if (!down || was) return false;   // auto-repeat never switches again
    const auto before = profile_;
    for (const auto& b : config_.bindings[std::size_t(profile_)][code]) {
        if (b.action == Action::ToggleProfile) profile_ = profile_ == Profile::OnFoot ? Profile::Driving : Profile::OnFoot;
        else if (b.action == Action::OnFoot) profile_ = Profile::OnFoot;
        else if (b.action == Action::Driving) profile_ = Profile::Driving;
    }
    if (profile_ != before) {
        // Keys held across the switch are ignored until released, so W held on
        // foot does not become full throttle in the car, and the switch key's
        // auto-repeat cannot switch back.
        suppressed_ |= down_;
        down_.reset();
        steer_ = 0;
        return true;
    }
    return false;
}

void Mapper::wheel(std::uint16_t code, int notches, std::int64_t nowUs) {
    if (code < kWheelUp || code > kWheelRight) return;
    for (int i = 0; i < notches && pulses_.size() < 8; ++i) pulses_.push_back({code, 0});
    if (!pulses_.empty() && pulses_.front().startUs == 0) pulses_.front().startUs = nowUs;
}

void Mapper::mouseMove(int dx, int dy, std::int64_t nowUs) {
    if (dx == 0 && dy == 0) return;
    if (moves_.size() >= 512) moves_.pop_front();
    moves_.push_back({nowUs, dx, dy});
}

void Mapper::forEachActive(auto&& fn) const {
    const auto& table = config_.bindings[std::size_t(profile_)];
    for (std::size_t code = 1; code < kCodeCount; ++code) {
        const bool active = down_[code] || (pulseCode_ && pulseCode_ == code);
        if (!active) continue;
        for (const auto& b : table[code]) if (b.action != Action::None) fn(b);
    }
}
bool Mapper::held(Action a) const {
    bool any = false;
    forEachActive([&](const Binding& b) { any = any || b.action == a; });
    return any;
}
std::uint32_t Mapper::heldButtons() const {
    std::uint32_t mask = 0;
    forEachActive([&](const Binding& b) { if (b.action == Action::Button) mask |= b.button; });
    return mask;
}

ControllerState Mapper::merge(const ControllerState& pad, std::int64_t nowUs) {
    const double dtMs = lastUs_ ? std::clamp(double(nowUs - lastUs_) / 1000.0, 0.0, 100.0) : 0.0;
    lastUs_ = nowUs;

    // Wheel pulses: each notch holds its binding for WheelPulseMs, then a gap of
    // the same length so that repeated notches are separate presses.
    pulseCode_ = 0;
    const auto pulseUs = std::int64_t(config_.wheelPulseMs * 1000);
    while (!pulses_.empty()) {
        auto& p = pulses_.front();
        if (p.startUs == 0) p.startUs = nowUs;
        if (nowUs < p.startUs + pulseUs) { pulseCode_ = p.code; break; }
        const auto nextStart = p.startUs + 2 * pulseUs;
        pulses_.pop_front();
        if (!pulses_.empty()) pulses_.front().startUs = std::max(nextStart, nowUs);
        if (!pulses_.empty() && nowUs < pulses_.front().startUs) break;
    }

    ControllerState out = pad;
    out.inputActive = true;
    out.buttons |= heldButtons();

    // Left stick from keys.
    double lx = (held(Action::LeftRight) ? 1 : 0) - (held(Action::LeftLeft) ? 1 : 0);
    double ly = (held(Action::LeftDown) ? 1 : 0) - (held(Action::LeftUp) ? 1 : 0);
    if (profile_ == Profile::Driving && config_.steerRampMs > 0) {
        const double target = lx;
        const bool returning = target == 0 || steer_ * target < 0;
        const double ms = returning ? config_.steerReturnMs : config_.steerRampMs;
        const double step = ms > 0 ? dtMs / ms : 1.0;
        steer_ = steer_ < target ? std::min(target, steer_ + step) : std::max(target, steer_ - step);
        lx = steer_;
    } else {
        steer_ = 0;
    }
    if (const double len = std::hypot(lx, ly); len > 1) { lx /= len; ly /= len; }
    if (held(Action::Walk)) { lx *= config_.walkScale; ly *= config_.walkScale; }
    const auto klx = axis(lx), kly = axis(ly);
    if (magnitude(klx, kly) > magnitude(out.leftX, out.leftY)) { out.leftX = klx; out.leftY = kly; }

    // Right stick from the mouse (keys bound to the right stick add a full deflection).
    const auto& m = config_.mouse;
    const auto windowUs = std::int64_t(m.windowMs * 1000);
    while (!moves_.empty() && moves_.front().tUs <= nowUs - windowUs) moves_.pop_front();
    double sx = 0, sy = 0;
    for (const auto& mv : moves_) { sx += mv.dx; sy += mv.dy; }
    double rx = 0, ry = 0;
    if (sx != 0 || sy != 0) {
        const double vx = sx / m.windowMs, vy = sy / m.windowMs * m.yRatio * (m.invertY ? -1 : 1);   // counts per ms
        const double speed = std::hypot(vx, vy);
        const double n = std::min(1.0, std::pow(speed * m.sensitivity / m.fullSpeed, m.exponent));
        const double deflection = m.deadzone + (1 - m.deadzone) * n;
        rx = vx / speed * deflection; ry = vy / speed * deflection;
    }
    rx += (held(Action::RightRight) ? 1 : 0) - (held(Action::RightLeft) ? 1 : 0);
    ry += (held(Action::RightDown) ? 1 : 0) - (held(Action::RightUp) ? 1 : 0);
    if (const double len = std::hypot(rx, ry); len > 1) { rx /= len; ry /= len; }
    const auto krx = axis(rx), kry = axis(ry);
    if (magnitude(krx, kry) > magnitude(out.rightX, out.rightY)) { out.rightX = krx; out.rightY = kry; }

    if (held(Action::L2)) out.l2 = 255;
    if (held(Action::R2)) out.r2 = 255;
    return out;
}

} // namespace veyra::remoteplay::kbm
