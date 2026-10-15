#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "brookesia/service_manager.hpp"
#include "brookesia/lib_utils.hpp"
#include "modules/power_manager.hpp"

using namespace esp_brookesia;
using namespace std::chrono_literals;

class SchedulerProbe : public service::ServiceBase {
public:
    SchedulerProbe() : ServiceBase({.name = "PwrTestProbe", .description = "Shared worker probe", .version = "1.0.0"}) {}
private:
    std::vector<service::FunctionSchema> get_function_schemas() override { return {{"Ping", "Check worker", {}}}; }
    FunctionHandlerList get_function_handlers() override
    {
        return {[](service::FunctionParameterMap &&) -> service::FunctionResult { return {.success = true}; }};
    }
};

template <class Predicate>
bool await(Predicate predicate, std::chrono::milliseconds timeout = 2500ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Pwr service check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

int main()
{
    PowerServiceInputs inputs;
    inputs.configure(1);
    inputs.set_ui_conditions(5, false, true);
    const auto initial = inputs.snapshot().activity_revision;
    for (int i = 0; i < 1000; ++i) inputs.set_ui_conditions(5, false, true);
    CHECK(inputs.snapshot().activity_revision == initial);
    std::thread first([&] { for (int i = 0; i < 2000; ++i) inputs.notify_activity(); });
    std::thread second([&] { for (int i = 0; i < 2000; ++i) inputs.notify_activity(); });
    first.join();
    second.join();
    CHECK(inputs.snapshot().activity_revision == initial + 4000);
    inputs.set_ui_conditions(6, true, true);
    CHECK(inputs.snapshot().activity_revision == initial + 4001);
    CHECK(inputs.snapshot().blocked);

    auto &manager = service::ServiceManager::get_instance();
    CHECK(manager.init());
    CHECK(manager.start());
    auto pwr = PowerManager::get_shared_instance();
    CHECK(!pwr->get_attributes().has_scheduler());
    CHECK(manager.add_service(pwr));
    auto binding = manager.bind(PowerManager::SERVICE_NAME);
    CHECK(binding.is_valid());
    std::atomic_uint events{0};
    auto connection = pwr->subscribe_event("StateChanged", [&](const std::string &, const service::EventItemMap &) {
        ++events;
    });
    CHECK(connection.connected());
    auto call = [&](const char *name, std::vector<service::FunctionValue> args = {}) {
        return pwr->call_function_sync(name, std::move(args), 1000);
    };
    CHECK(!call("Configure", {-1.0}).success);
    CHECK(!call("SetIdleConditions", {-1.0, false, true}).success);
    CHECK(call("Configure", {1.0}).success);
    std::this_thread::sleep_for(120ms);
    CHECK(pwr->state() == PowerPolicyState::Boot);
    pwr->set_host_agent_idle(false);
    CHECK(call("SetIdleConditions", {1.0, false, true}).success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    // No LVGL worker, UI poll, or tick is running in this executable.
    CHECK(await([&] { return pwr->state() == PowerPolicyState::WaitAgentSleep; }));
    pwr->set_host_agent_idle(true);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Dimmed; }));
    CHECK(pwr->host_backlight_pwm() == 10);
    CHECK(call("SetIdleConditions", {1.0, true, true}).success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(pwr->host_backlight_pwm() == 100);
    CHECK(call("SetIdleConditions", {1.0, false, true}).success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::HardwareSleep; }));
    CHECK(pwr->host_backlight_pwm() == 0);
    auto probe = std::make_shared<SchedulerProbe>();
    CHECK(manager.add_service(probe));
    auto probe_binding = manager.bind("PwrTestProbe");
    CHECK(probe_binding.is_valid());
    CHECK(probe->call_function_sync("Ping", std::vector<service::FunctionValue>{}, 500).success);
    // Multiple producers update one fixed-size mailbox without queueing each activity.
    std::thread touch([&] { for (int i = 0; i < 2000; ++i) pwr->notify_user_activity(); });
    std::thread imu([&] { for (int i = 0; i < 2000; ++i) pwr->notify_user_activity(); });
    touch.join();
    imu.join();
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(pwr->host_backlight_pwm() == 100);
    CHECK(call("GetState").success);
    CHECK(call("NotifyActivity").success);
    // A failed dim operation recovers Active, then retries restore on its deadline.
    pwr->fail_next_host_backlight();
    std::this_thread::sleep_for(1150ms);
    CHECK(pwr->state() == PowerPolicyState::Active);
    CHECK(pwr->host_backlight_pwm() == 100);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::HardwareSleep; }, 4000ms));
    pwr->fail_next_host_backlight();
    CHECK(call("NotifyActivity").success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    CHECK(await([&] { return pwr->host_backlight_pwm() == 100; }));
    CHECK(events.load() >= 6);
    // Stop cancels periodic work and restores the temporary PWM override.
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Dimmed; }));
    binding = {};
    probe_binding = {};
    CHECK(!pwr->is_running());
    CHECK(pwr->host_backlight_pwm() == 100);
    std::this_thread::sleep_for(650ms);
    CHECK(pwr->host_backlight_pwm() == 100);
    binding = manager.bind(PowerManager::SERVICE_NAME);
    CHECK(binding.is_valid());
    CHECK(call("SetIdleConditions", {2.0, true, true}).success);
    CHECK(await([&] { return pwr->state() == PowerPolicyState::Active; }));
    binding = {};
    connection.disconnect();
    manager.stop();
    std::puts("Pwr service lifecycle, independent scheduling, wake, blockers and backlight recovery passed");
    return 0;
}
