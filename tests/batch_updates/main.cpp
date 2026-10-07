#include <nana/gui/programming_interface.hpp>
#include <nana/gui/widgets/button.hpp>
#include <nana/gui/widgets/form.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class DrawingProbe {
public:
    DrawingProbe(nana::window window, std::function<void(nana::paint::graphics&)> callback)
        : window_{window}, handle_{nana::api::drawing(window, std::move(callback))} {}
    ~DrawingProbe() { nana::api::remove_drawing(window_, handle_); }
    DrawingProbe(const DrawingProbe&) = delete;
    DrawingProbe& operator=(const DrawingProbe&) = delete;

private:
    nana::window window_;
    nana::drawing_handle handle_;
};

struct Fixture {
    nana::form root{nana::rectangle{-32000, -32000, 280, 160},
        nana::appearance{false, false, false, true, false, false, false}};
    nana::button first{root, nana::rectangle{10, 10, 110, 35}};
    nana::button second{root, nana::rectangle{145, 10, 110, 35}};
    nana::button inner{root, nana::rectangle{10, 65, 110, 35}};

    Fixture() {
        first.transparent(true);
        second.transparent(true);
        first.caption("First");
        second.caption("Second");
#ifdef _WIN32
        const auto extended = GetWindowLongPtrW(native(), GWL_EXSTYLE);
        SetWindowLongPtrW(native(), GWL_EXSTYLE,
            (extended & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW)) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        expect(SetWindowPos(native(), HWND_NOTOPMOST, -32000, -32000, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE,
            "Could not place the fixture offscreen");
#endif
        root.show();
#ifdef _WIN32
        // STARTUPINFO may override the first ShowWindow call in a test runner.
        ShowWindow(native(), SW_SHOWNOACTIVATE);
        MSG message{};
        for (unsigned count = 0; count < 2000 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            if (message.message == WM_QUIT) break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        expect(IsWindowVisible(native()) != FALSE, "Native fixture is not shown");
        expect((GetWindowLongPtrW(native(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0,
            "Fixture must not activate a desktop window");
        RECT bounds{};
        expect(GetWindowRect(native(), &bounds) != FALSE && bounds.right < -10000,
            "Native fixture is not offscreen");
#endif
        nana::api::refresh_window_tree(root);
        expect(nana::api::visible(root), "Nana fixture is not shown");
    }

#ifdef _WIN32
    HWND native() const { return reinterpret_cast<HWND>(root.native_handle()); }
#endif
};

void self_refresh() {
    Fixture fixture;
    unsigned draws{};
    bool active{};
    DrawingProbe drawing{fixture.first, [&](nana::paint::graphics&) {
        if (!active) return;
        // Bound generated requests so a regression fails instead of hanging.
        if (++draws > 8) return;
        nana::api::refresh_window(fixture.first);
        nana::api::batch_updates(fixture.first, [&] { nana::api::refresh_window(fixture.first); });
    }};
    active = true;
    nana::api::batch_updates(fixture.root, [&] { nana::api::refresh_window(fixture.first); });
    active = false;
    expect(draws == 1, "A drawing callback repeated its active requester");
}

void mutual_refresh_impl(bool native_event) {
    Fixture fixture;
    unsigned first_draws{}, second_draws{}, events{};
    bool active{};
    std::vector<unsigned> order;
    DrawingProbe first{fixture.first, [&](nana::paint::graphics&) {
        if (!active) return;
        order.push_back(1);
        if (++first_draws <= 8) nana::api::refresh_window(fixture.second);
    }};
    DrawingProbe second{fixture.second, [&](nana::paint::graphics&) {
        if (!active) return;
        order.push_back(2);
        if (++second_draws <= 8) nana::api::refresh_window(fixture.first);
    }};
    const auto update = [&] {
        ++events;
        active = true;
        nana::api::refresh_window(fixture.first);
    };
#ifdef _WIN32
    if (native_event) fixture.first.events().mouse_move(update);
#endif
    // Deduplication ends at the flush boundary; an independent second update
    // must redraw both widgets again without a message pump or forced repaint.
    for (unsigned pass = 0; pass != 2; ++pass) {
        first_draws = second_draws = events = 0;
        order.clear();
#ifdef _WIN32
        if (native_event) {
            // Exercise Nana's real native root_guard without moving the mouse.
            SendMessageW(fixture.native(), WM_MOUSEMOVE, 0, MAKELPARAM(15 + pass, 15));
        } else
#endif
        {
            nana::api::batch_updates(fixture.root, update);
        }
        active = false;
        expect(events == 1 && first_draws == 1 && second_draws == 1,
            "Mutual refresh repeated a requester or suppressed an independent batch");
        expect(order == std::vector<unsigned>{1, 2}, "Mutual refresh changed callback order");
    }
}

void mutual_refresh() { mutual_refresh_impl(false); }

void nested_batch() {
    Fixture fixture;
    unsigned first_draws{}, second_draws{};
    bool active{};
    bool second_ran_inside_first{};
    std::vector<unsigned> order;
    DrawingProbe first{fixture.first, [&](nana::paint::graphics&) {
        if (!active || ++first_draws > 8) return;
        order.push_back(1);
        nana::api::batch_updates(fixture.inner, [&] {
            fixture.inner.caption("Nested");
            nana::api::refresh_window(fixture.first);
            nana::api::refresh_window(fixture.second);
            order.push_back(2);
        });
        second_ran_inside_first = second_ran_inside_first || second_draws != 0;
        order.push_back(3);
    }};
    DrawingProbe second{fixture.second, [&](nana::paint::graphics&) {
        if (!active) return;
        ++second_draws;
        order.push_back(4);
    }};
    active = true;
    nana::api::batch_updates(fixture.root, [&] {
        nana::api::refresh_window(fixture.first);
        nana::api::refresh_window(fixture.second);
        expect(order.empty(), "Transparent callbacks ran before the batch action completed");
    });
    active = false;
    expect(!second_ran_inside_first, "Nested same-root batch recursively drained pending callbacks");
    expect(first_draws == 1 && second_draws == 1 && order == std::vector<unsigned>{1, 2, 3, 4},
        "Nested batch lost, repeated or reordered pending callbacks");
}

void cross_root() {
    Fixture fixture;
    Fixture other;
    unsigned first_draws{}, other_draws{}, second_draws{};
    bool active{};
    bool other_completed{};
    bool second_ran_inside_first{};
    std::vector<unsigned> order;
    DrawingProbe first{fixture.first, [&](nana::paint::graphics&) {
        if (!active || ++first_draws > 8) return;
        order.push_back(1);
        nana::api::batch_updates(other.root, [&] { nana::api::refresh_window(other.first); });
        other_completed = other_draws == 1;
        second_ran_inside_first = second_ran_inside_first || second_draws != 0;
        order.push_back(3);
    }};
    DrawingProbe other_first{other.first, [&](nana::paint::graphics&) {
        if (!active || ++other_draws > 8) return;
        order.push_back(2);
        nana::api::batch_updates(fixture.root, [&] { nana::api::refresh_window(fixture.second); });
    }};
    DrawingProbe second{fixture.second, [&](nana::paint::graphics&) {
        if (!active) return;
        ++second_draws;
        order.push_back(4);
    }};
    active = true;
    nana::api::batch_updates(fixture.root, [&] {
        nana::api::refresh_window(fixture.first);
        nana::api::refresh_window(fixture.second);
    });
    active = false;
    expect(other_completed, "An active flush prevented a different root from completing its batch");
    expect(!second_ran_inside_first && first_draws == 1 && second_draws == 1 && other_draws == 1
        && order == std::vector<unsigned>{1, 2, 3, 4}, "Cross-root callbacks reentered the first root's flush");
}

void exception_recovery() {
    Fixture fixture;
    struct ExpectedFailure { const void* identity; };
    unsigned draws{}, token{};
    bool active{};
    bool caught{};
    DrawingProbe drawing{fixture.first, [&](nana::paint::graphics&) { if (active) ++draws; }};
    active = true;
    try {
        nana::api::batch_updates(fixture.root, [&] {
            nana::api::refresh_window(fixture.first);
            throw ExpectedFailure{&token};
        });
    } catch (const ExpectedFailure& error) {
        caught = error.identity == &token;
    }
    active = false;
    expect(caught && draws == 1, "Failed action did not flush once and preserve its original exception");
    active = true;
    nana::api::batch_updates(fixture.root, [&] { nana::api::refresh_window(fixture.first); });
    active = false;
    expect(draws == 2, "Failed action left updates disabled for the next independent batch");
}

#ifdef _WIN32
void native_event() { mutual_refresh_impl(true); }

void native_event_inside_batch() {
    Fixture fixture;
    // Establish hover on an opaque event source before observing the separate
    // transparent requesters. Its normal hover redraw is a buffer update.
    SendMessageW(fixture.native(), WM_MOUSEMOVE, 0, MAKELPARAM(15, 70));
    unsigned first_draws{}, second_draws{}, events{};
    bool active{};
    bool action_completed{};
    bool drew_inside_action{};
    DrawingProbe first{fixture.first, [&](nana::paint::graphics&) {
        if (!active) return;
        ++first_draws;
        drew_inside_action = drew_inside_action || !action_completed;
    }};
    DrawingProbe second{fixture.second, [&](nana::paint::graphics&) {
        if (!active) return;
        ++second_draws;
        drew_inside_action = drew_inside_action || !action_completed;
    }};
    fixture.inner.events().mouse_move([&] {
        ++events;
        fixture.second.caption("Native handler");
        nana::api::refresh_window(fixture.first);
        nana::api::refresh_window(fixture.second);
    });
    active = true;
    nana::api::batch_updates(fixture.root, [&] {
        nana::api::refresh_window(fixture.first);
        nana::api::refresh_window(fixture.second);
        expect(first_draws == 0 && second_draws == 0, "Pending widgets painted before the native event");
        // Synchronous native notifications can nest inside an action without
        // pumping messages. Their root_guard must preserve the outer batch.
        SendMessageW(fixture.native(), WM_MOUSEMOVE, 0, MAKELPARAM(16, 70));
        expect(events == 1, "Nested native event did not reach its widget handler");
        expect(first_draws == 0 && second_draws == 0, "Native event drained an active batch");
        fixture.first.caption("After native event");
        fixture.second.caption("Final second");
        nana::api::refresh_window(fixture.first);
        nana::api::refresh_window(fixture.second);
        expect(first_draws == 0 && second_draws == 0, "Native event ended the surrounding batch early");
        action_completed = true;
    });
    active = false;
    expect(!drew_inside_action && first_draws == 1 && second_draws == 1,
        "Nested native event lost or repeated the final batch repaint");
    expect(fixture.first.caption() == "After native event" && fixture.second.caption() == "Final second",
        "Updates after the nested native event were not retained");
}
#endif
} // namespace

int main(int argc, char** argv) {
    using Test = std::pair<std::string_view, void (*)()>;
    const std::array tests{
        Test{"self_refresh", self_refresh}, Test{"mutual_refresh", mutual_refresh},
        Test{"nested_batch", nested_batch}, Test{"cross_root", cross_root},
        Test{"exception_recovery", exception_recovery},
#ifdef _WIN32
        Test{"native_event", native_event},
        Test{"native_event_inside_batch", native_event_inside_batch},
#endif
    };
    if (argc != 2) {
        std::cerr << "Pass one batching regression scenario name.\n";
        return 2;
    }
    for (const auto& [name, test] : tests) {
        if (name != argv[1]) continue;
        try {
            test();
            std::cout << "PASS " << name << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        } catch (...) {
            std::cerr << "FAIL " << name << ": unexpected exception\n";
        }
        return 1;
    }
    std::cerr << "Unknown batching regression scenario: " << argv[1] << '\n';
    return 2;
}
