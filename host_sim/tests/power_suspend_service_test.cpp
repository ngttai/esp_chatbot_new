#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include "brookesia/service_manager.hpp"
#include "modules/power_manager.hpp"

using namespace esp_brookesia;
using namespace std::chrono_literals;
using C = SuspendCoordinator;

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "Pwr suspend service failed at %d: %s\n", __LINE__, #test); return 1; } } while (false)
template<class F> bool await(F predicate, std::chrono::milliseconds timeout = 2500ms)
{
    auto until = std::chrono::steady_clock::now() + timeout;
    do { if (predicate()) return true; std::this_thread::sleep_for(5ms); }
    while (std::chrono::steady_clock::now() < until);
    return predicate();
}

enum class Mode { HoldPrepare, Auto, Busy, HoldCommit, HoldResume };

int main()
{
    auto &manager = service::ServiceManager::get_instance();
    CHECK(manager.init() && manager.start());
    auto pwr = PowerManager::get_shared_instance();
    CHECK(manager.add_service(pwr));
    auto binding = manager.bind("Pwr");
    CHECK(binding.is_valid());
    std::atomic<Mode> mode{Mode::HoldPrepare};
    std::atomic_uint prepares{0}, commits{0}, cancels{0}, resumes{0}, errors{0};
    std::atomic_uint32_t last_session{0};
    std::atomic_bool fail_on_commit{false};
    std::vector<service::EventSignalConnection> connections;
    auto acknowledge = [&](uint32_t session, C::Report status) {
        for (uint8_t id = 1; id <= 3; ++id) {
            if (!pwr->report_suspend_status(session, id, status)) ++errors;
        }
    };
    for (const char *name : {"PrepareSuspend", "CommitSuspend", "CancelSuspend", "Resume"}) {
        auto connection = pwr->subscribe_event(name, [&](const std::string &event, const service::EventItemMap &items) {
            const auto session = static_cast<uint32_t>(std::get<double>(items.at("SessionId")));
            if (std::get<double>(items.at("ParticipantMask")) != 7) ++errors;
            last_session = session;
            const auto selected = mode.load();
            if (event == "PrepareSuspend") {
                ++prepares;
                if (selected == Mode::Busy) {
                    if (!pwr->report_suspend_status(session, 1, C::Report::Busy)) ++errors;
                } else if (selected != Mode::HoldPrepare) acknowledge(session, C::Report::Ready);
            } else if (event == "CommitSuspend") {
                ++commits;
                if (fail_on_commit.exchange(false)) pwr->fail_next_host_backlight();
                if (selected != Mode::HoldCommit) acknowledge(session, C::Report::Suspended);
            } else {
                if (event == "CancelSuspend") ++cancels;
                else ++resumes;
                if (selected != Mode::HoldResume) acknowledge(session, C::Report::Resumed);
            }
        });
        CHECK(connection.connected());
        connections.push_back(std::move(connection));
    }
    auto call = [&](const char *name, std::vector<service::FunctionValue> args = {}) {
        return pwr->call_function_sync(name, std::move(args), 500);
    };
    CHECK(call("Configure", {1.0}).success);
    for (double id : {1.0, 2.0, 3.0}) CHECK(call("RegisterParticipant", {id}).success);
    CHECK(!call("RegisterParticipant", {1.0}).success);
    CHECK(!call("RegisterParticipant", {9.0}).success);
    pwr->set_ui_conditions(1, false, true);
    CHECK(await([&] { return prepares.load() == 1; }));
    const auto first = last_session.load();
    CHECK(pwr->state() == PowerPolicyState::Dimmed && pwr->host_backlight_pwm() == 10);
    CHECK(call("GetSuspendStatus").success);
    CHECK(!call("UnregisterParticipant", {1.0}).success);
    CHECK(!call("ReportSuspendStatus", {static_cast<double>(first + 1), 1.0, std::string("Ready")}).success);
    CHECK(call("ReportSuspendStatus", {static_cast<double>(first), 1.0, std::string("Ready")}).success);
    CHECK(call("ReportSuspendStatus", {static_cast<double>(first), 1.0, std::string("Ready")}).success);
    pwr->set_ui_conditions(2, true, true);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(cancels == 1 && commits == 0 && pwr->host_backlight_pwm() == 100);
    CHECK(!pwr->report_suspend_status(first, 2, C::Report::Ready));

    // Silence causes Prepare timeout, rollback acknowledgement, then bounded backoff.
    pwr->set_ui_conditions(2, false, true);
    CHECK(await([&] { return pwr->suspend_status().phase == C::Phase::Backoff; }));
    CHECK(pwr->suspend_status().reason == C::Reason::PrepareTimeout);
    const auto count = prepares.load();
    CHECK(pwr->state() == PowerPolicyState::Dimmed && pwr->host_backlight_pwm() == 10);
    std::this_thread::sleep_for(150ms);
    CHECK(prepares == count);
    mode = Mode::Busy;
    CHECK(await([&] { return pwr->suspend_status().phase == C::Phase::Backoff && prepares > count; }));
    CHECK(pwr->suspend_status().reason == C::Reason::Busy);

    mode = Mode::HoldCommit;
    CHECK(await([&] { return pwr->suspend_status().phase == C::Phase::Committing; }));
    CHECK(await([&] { return commits.load() >= 1; }));
    CHECK(pwr->state() == PowerPolicyState::Dimmed && pwr->host_backlight_pwm() == 10);
    pwr->set_ui_conditions(3, true, true);
    std::this_thread::sleep_for(70ms);
    CHECK(pwr->suspend_status().phase == C::Phase::Committing);
    acknowledge(last_session, C::Report::Suspended);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(pwr->host_backlight_pwm() == 100);

    // A missing Resume acknowledgement keeps hardware dark until explicit recovery.
    mode = Mode::HoldResume;
    pwr->set_ui_conditions(3, false, true);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::HardwareSleep; }));
    CHECK(pwr->host_backlight_pwm() == 0);
    pwr->set_ui_conditions(4, true, true);
    CHECK(await([&] { return pwr->suspend_status().phase == C::Phase::Recovery; }));
    CHECK(pwr->state() == PowerPolicyState::HardwareSleep && pwr->host_backlight_pwm() == 0);
    const auto recovery = pwr->suspend_status().session;
    CHECK(!call("RetryResume", {static_cast<double>(recovery + 1)}).success);
    mode = Mode::Auto;
    CHECK(call("RetryResume", {static_cast<double>(recovery)}).success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(last_session != recovery && pwr->host_backlight_pwm() == 100);
    CHECK(!pwr->report_suspend_status(recovery, 1, C::Report::Resumed));

    fail_on_commit = true;
    pwr->set_ui_conditions(4, false, true);
    CHECK(await([&] { return pwr->suspend_status().reason == C::Reason::ActuatorError &&
                            pwr->state() == PowerPolicyState::Active; }));
    CHECK(pwr->host_backlight_pwm() == 100);

    // Stop/restart cannot accept acknowledgements from an interrupted generation.
    mode = Mode::HoldPrepare;
    pwr->set_ui_conditions(4, false, true);
    CHECK(await([&] { return pwr->suspend_status().phase == C::Phase::Preparing; }));
    const auto interrupted = pwr->suspend_status().session;
    binding = {};
    CHECK(pwr->suspend_status().phase == C::Phase::Recovery);
    CHECK(!pwr->report_suspend_status(interrupted, 1, C::Report::Ready));
    binding = manager.bind("Pwr");
    CHECK(binding.is_valid());
    pwr->set_ui_conditions(5, true, true);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(pwr->host_backlight_pwm() == 100);
    CHECK(errors == 0);
    CHECK(call("UnregisterParticipant", {1.0}).success);
    CHECK(call("UnregisterParticipant", {2.0}).success);
    CHECK(call("UnregisterParticipant", {3.0}).success);
    CHECK(pwr->suspend_status().registered == 0);
    binding = {};
    for (auto &connection : connections) connection.disconnect();
    manager.stop();
    std::puts("Pwr suspend service events/API, readiness gate, deadlines, rollback, wake and restart passed with fake participants");
}
