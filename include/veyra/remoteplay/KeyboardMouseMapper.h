// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 键鼠控制 (keyboard and mouse as a PS5 controller) for PS5 手柄控制.
//
// Turns held keys, mouse buttons, wheel notches and relative mouse movement into
// a ControllerState, which is then merged with the real pad (if any) and sent over
// the controller-only Remote Play session. No Win32 and no Qt: the capture layer
// (ui::KeyboardMouseCapture) feeds already decoded events with their arrival time,
// so the whole translation is testable with synthesized input.
//
// Two binding profiles, because a game such as GTA needs W/S on the left stick on
// foot but on R2/L2 in a car. A key bound to ToggleProfile / OnFoot / Driving
// switches; the mapping file can put that on the same key as entering a vehicle.
//
// Mouse → right stick: the stick is a rate control (deflection = turn speed), so
// the mouse speed over a short window is converted to a deflection with an
// anti-deadzone (games ignore the first part of the stick travel), a response
// exponent and a saturation speed. When the mouse stops the stick returns to the
// centre within one window.
#include "Types.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::remoteplay::kbm {

// Input codes: Windows virtual-key codes (mouse buttons included: 0x01 left, 0x02
// right, 0x04 middle, 0x05/0x06 X1/X2) and four wheel pseudo codes.
constexpr std::uint16_t kWheelUp = 0x100, kWheelDown = 0x101, kWheelLeft = 0x102, kWheelRight = 0x103;
constexpr std::size_t kCodeCount = 0x104;

enum class Action : std::uint8_t {
    None, Button,
    LeftUp, LeftDown, LeftLeft, LeftRight,
    RightUp, RightDown, RightLeft, RightRight,
    L2, R2,
    Walk,            // held: the left stick only goes WalkScale far (walk instead of run)
    ToggleProfile, OnFoot, Driving
};
struct Binding {
    Action action = Action::None;
    std::uint32_t button = 0;   // ControllerState::Button bit for Action::Button
    bool operator==(const Binding&) const = default;
};
enum class Profile : std::uint8_t { OnFoot = 0, Driving = 1 };

struct MouseTuning {
    double sensitivity = 1.0;   // set from the UI slider (percent / 100)
    double yRatio = 1.0;        // vertical speed relative to horizontal
    double exponent = 1.0;      // <1 boosts small movements, >1 makes them finer
    double deadzone = 0.15;     // anti-deadzone: smallest deflection sent for any movement
    double fullSpeed = 4.0;     // mouse counts per ms that give full deflection at sensitivity 1
    double windowMs = 12.0;     // speed is measured over this window; also the stop latency
    bool invertY = false;
};

struct Config {
    // Up to three actions per input code and profile.
    using Slot = std::array<Binding, 3>;
    std::array<std::array<Slot, kCodeCount>, 2> bindings{};
    MouseTuning mouse;
    double walkScale = 0.45;
    double steerRampMs = 140;   // driving: A/D reach full lock in this time (0 = instant)
    double steerReturnMs = 80;  // ... and return to centre / reverse this fast
    double wheelPulseMs = 50;   // one wheel notch holds its binding this long
    bool bound(std::uint16_t code) const;
};

struct ParseResult {
    Config config;
    std::vector<std::string> warnings;   // "line N: ..." (unknown key, action or value)
};
// INI text: [General] [Mouse] [OnFoot] [Driving]; "Key = Action[, Action...]".
// Sections may also be named 步行 / 驾驶. Keys and actions are case-insensitive.
ParseResult parseConfig(std::string_view text);
// The default mapping (GTA-style PS layout), with comments; written for the user to edit.
std::string_view defaultConfigText();
// 0 when the name is unknown.
std::uint16_t codeForName(std::string_view name);
std::string nameForCode(std::uint16_t code);

class Mapper {
public:
    void setConfig(const Config& config);
    const Config& config() const { return config_; }
    void setSensitivity(double s);

    // Events, with their arrival time in microseconds (any monotonic clock).
    // Returns true when the event switched the profile.
    bool key(std::uint16_t code, bool down, std::int64_t nowUs);
    void wheel(std::uint16_t code, int notches, std::int64_t nowUs);
    void mouseMove(int dx, int dy, std::int64_t nowUs);

    // The controller state for this moment, merged with the pad's: buttons are
    // combined, each stick takes the larger deflection, triggers the larger value.
    ControllerState merge(const ControllerState& pad, std::int64_t nowUs);

    void reset();   // release everything (capture ended); keeps the profile
    Profile profile() const { return profile_; }
    void setProfile(Profile p) { profile_ = p; }

private:
    bool held(Action a) const;
    std::uint32_t heldButtons() const;
    void forEachActive(auto&& fn) const;

    Config config_;
    std::bitset<kCodeCount> down_, suppressed_;
    struct Pulse { std::uint16_t code; std::int64_t startUs; };
    std::deque<Pulse> pulses_;
    std::uint16_t pulseCode_ = 0;   // wheel code currently held by a pulse (0 none)
    struct Move { std::int64_t tUs; int dx, dy; };
    std::deque<Move> moves_;
    Profile profile_ = Profile::OnFoot;
    double steer_ = 0;              // driving: ramped left stick X
    std::int64_t lastUs_ = 0;
};

} // namespace veyra::remoteplay::kbm
