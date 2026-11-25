// Standalone checks for 键鼠控制 (KeyboardMouseMapper). No Win32, SDL, Qt or console.
#include "veyra/remoteplay/KeyboardMouseMapper.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace veyra::remoteplay;
using namespace veyra::remoteplay::kbm;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
constexpr std::int64_t ms(double v) { return std::int64_t(v * 1000); }
Mapper defaultMapper() {
    Mapper m;
    m.setConfig(parseConfig(defaultConfigText()).config);
    m.setSensitivity(1.0);
    return m;
}
}

int main() {
    {
        const auto parsed = parseConfig(defaultConfigText());
        for (const auto& w : parsed.warnings) std::printf("  warning: %s\n", w.c_str());
        check(parsed.warnings.empty(), "default mapping parses without warnings");
        check(parsed.config.bound('W') && parsed.config.bound(0x01) && parsed.config.bound(kWheelUp), "default binds W, left mouse, wheel");
        check(!parsed.config.bound(0x77) && !parsed.config.bound(0x78), "F8/F9 stay free for Veyra");
    }
    {
        const auto parsed = parseConfig("[OnFoot]\nBogusKey = Cross\nW = Nope, Cross\n[Weird]\n[Mouse]\nCurve = abc\nDeadzone = 9\n");
        check(parsed.warnings.size() == 4, "unknown key/action/section/number each warn");
        check(parsed.config.mouse.deadzone == 0.5, "out-of-range values are clamped");
        check(parsed.config.bindings[0]['W'][0].action == Action::Button, "valid actions on a line with a bad one are kept");
    }
    check(codeForName("f12") == 0x7B && codeForName("Numpad3") == 0x63 && codeForName("a") == 'A' && codeForName("F25") == 0, "key names");
    check(nameForCode(0x7B) == "F12" && nameForCode(0x01) == "MouseLeft", "code names");
    {
        auto m = defaultMapper();
        ControllerState pad;
        auto s = m.merge(pad, ms(0));
        check(s.inputActive && s.buttons == 0 && s.leftX == 0 && s.rightX == 0 && s.l2 == 0, "idle is neutral but active");
        m.key('W', true, ms(1)); m.key('D', true, ms(1));
        s = m.merge(pad, ms(8));
        const double len = std::hypot(double(s.leftX), double(s.leftY)) / 32767.0;
        check(s.leftX > 0 && s.leftY < 0 && std::abs(len - 1.0) < 0.01, "W+D is a full diagonal, not 141%");
        m.key(0x12, true, ms(9));   // Alt = Walk
        s = m.merge(pad, ms(16));
        check(std::abs(std::hypot(double(s.leftX), double(s.leftY)) / 32767.0 - 0.45) < 0.01, "Alt walks at WalkScale");
        m.key(0x01, true, ms(17)); m.key(0x10, true, ms(17));   // MouseLeft = R2, Shift = Cross
        s = m.merge(pad, ms(24));
        check(s.r2 == 255 && (s.buttons & ControllerState::Cross), "mouse left is R2, Shift is Cross");
        m.reset();
        s = m.merge(pad, ms(32));
        check(s.buttons == 0 && s.leftX == 0 && s.r2 == 0, "reset releases everything");
    }
    {
        auto m = defaultMapper();
        ControllerState pad;
        m.merge(pad, ms(0));
        m.mouseMove(1, 0, ms(1));
        auto s = m.merge(pad, ms(2));
        check(s.rightX >= std::lround(0.15 * 32767) - 1 && s.rightX < 0.2 * 32767, "tiny mouse movement still clears the anti-deadzone");
        for (int t = 0; t < 12; ++t) m.mouseMove(10, 0, ms(10 + t));   // 10 counts/ms
        s = m.merge(pad, ms(22));
        check(s.rightX == 32767 && s.rightY == 0, "fast movement saturates the stick");
        s = m.merge(pad, ms(40));
        check(s.rightX == 0 && s.rightY == 0, "stick recentres one window after the mouse stops");
        for (int t = 0; t < 12; ++t) m.mouseMove(0, 2, ms(51 + t));   // 2 counts/ms down = half of FullSpeed
        s = m.merge(pad, ms(62));
        const double expect = 0.15 + 0.85 * 0.5;
        check(std::abs(s.rightY / 32767.0 - expect) < 0.02 && s.rightX == 0, "linear response between deadzone and full speed");
        m.setSensitivity(2.0);
        for (int t = 0; t < 12; ++t) m.mouseMove(0, 2, ms(81 + t));
        s = m.merge(pad, ms(92));
        check(s.rightY == 32767, "sensitivity scales the speed");
    }
    {
        auto m = defaultMapper();
        ControllerState pad;
        m.merge(pad, ms(0));
        m.key('W', true, ms(1));
        check(m.key(0xC0, true, ms(2)) && m.profile() == Profile::Driving, "Grave switches to driving");
        auto s = m.merge(pad, ms(8));
        check(s.r2 == 0 && s.leftY == 0, "W held across the switch is ignored until released");
        m.key(0xC0, true, ms(9));   // auto-repeat of the switch key
        check(m.profile() == Profile::Driving, "switch key auto-repeat does not switch back");
        m.key(0xC0, false, ms(10));
        m.key('W', false, ms(10)); m.key('W', true, ms(11));
        s = m.merge(pad, ms(16));
        check(s.r2 == 255 && s.leftY == 0, "driving: W is throttle (R2)");
        m.key('D', true, ms(16));
        s = m.merge(pad, ms(16 + 70));   // half of SteerRampMs
        check(std::abs(s.leftX / 32767.0 - 0.5) < 0.03, "driving: steering ramps up");
        s = m.merge(pad, ms(16 + 200));
        check(s.leftX == 32767, "driving: steering reaches full lock");
        m.key('D', false, ms(216));
        s = m.merge(pad, ms(216 + 40));
        check(std::abs(s.leftX / 32767.0 - 0.5) < 0.03, "driving: steering returns faster");
        m.key(0xC0, true, ms(300));
        check(m.profile() == Profile::OnFoot, "Grave switches back to on foot");
    }
    {
        auto m = defaultMapper();
        ControllerState pad;
        m.merge(pad, ms(0));
        m.wheel(kWheelDown, 2, ms(1));
        int presses = 0; bool was = false;
        for (int t = 1; t < 400; t += 4) {
            const bool now = (m.merge(pad, ms(t)).buttons & ControllerState::Down) != 0;
            if (now && !was) ++presses;
            was = now;
        }
        check(presses == 2 && !was, "two wheel notches are two separate D-pad presses");
    }
    {
        auto m = defaultMapper();
        ControllerState pad;
        pad.inputActive = true; pad.leftX = 30000; pad.buttons = ControllerState::Triangle; pad.l2 = 100;
        m.merge(pad, ms(0));
        m.key('A', true, ms(1)); m.key(0x02, true, ms(1));   // A = left, MouseRight = L2
        auto s = m.merge(pad, ms(8));
        check(s.leftX == -32768 && (s.buttons & ControllerState::Triangle) && s.l2 == 255, "merge: larger stick wins, buttons combine, triggers max");
        m.key('A', false, ms(9)); m.key(0x02, false, ms(9));
        s = m.merge(pad, ms(16));
        check(s.leftX == 30000 && s.l2 == 100, "merge: real pad passes through when keys are up");
    }
    {
        Mapper m;
        m.setConfig(parseConfig("[OnFoot]\nF = Triangle, Driving\n[Driving]\nF = Triangle, OnFoot\n").config);
        ControllerState pad;
        m.merge(pad, ms(0));
        m.key('F', true, ms(1));
        check(m.profile() == Profile::Driving, "F = Triangle, Driving switches on press");
        check(m.merge(pad, ms(8)).buttons == 0, "the switching press is held back (re-press to use the new binding)");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
