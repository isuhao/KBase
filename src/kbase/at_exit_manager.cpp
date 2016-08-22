/*
 @ 0xCCCCCCCC
*/

#include "kbase/at_exit_manager.h"

#include "kbase/error_exception_util.h"

namespace kbase {

static AtExitManager* g_top_exit_manager = nullptr;

AtExitManager::AtExitManager() : next_at_exit_manager_(g_top_exit_manager)
{
    // Normally, there is only one AtExitManager instance for a module.
    ENSURE(CHECK, g_top_exit_manager == nullptr).Require();
    g_top_exit_manager = this;
}

AtExitManager::~AtExitManager()
{
    bool top_manager_intact = g_top_exit_manager == this;
    ENSURE(CHECK, top_manager_intact).Require();
    if (top_manager_intact) {
        ProcessCallbackNow();
        g_top_exit_manager = next_at_exit_manager_;
    }
}

// static
void AtExitManager::RegisterCallback(const AtExitCallback& callback)
{
    ENSURE(CHECK, g_top_exit_manager != nullptr).Require();

    std::lock_guard<std::mutex> lock(g_top_exit_manager->lock_);
    g_top_exit_manager->callback_stack_.push(callback);
}

// static
void AtExitManager::ProcessCallbackNow()
{
    ENSURE(CHECK, g_top_exit_manager != nullptr).Require();
    if (!g_top_exit_manager) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_top_exit_manager->lock_);

    while (!g_top_exit_manager->callback_stack_.empty()) {
        auto task = g_top_exit_manager->callback_stack_.top();
        task();
        g_top_exit_manager->callback_stack_.pop();
    }
}

}   // namespace kbase