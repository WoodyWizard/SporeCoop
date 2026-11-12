// The removal of queued tasks (console.cpp drop_tasks_in): portable, tests/validate_tests.cpp runs it on Linux.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <vector>

namespace sk {

// BUG-0860: the tasks of every queue in `queues` for which `match(task)` holds are taken out under `mu`. With
// `outside` (kit.fix taskdrop) they are destroyed after `mu` is unlocked: a task's captures may queue another task in
// their destructors (run_on_main locks the same, non-recursive mutex), which deadlocked the thread before (the tasks
// were erased while the lock was held). Returns how many went.
template <class Mutex, class Queue, class Match>
size_t drop_tasks(Mutex& mu, std::initializer_list<Queue*> queues, Match match, bool outside)
{
    std::vector<typename Queue::value_type> gone;
    size_t n = 0;
    {
        std::lock_guard<Mutex> lk(mu);
        for (Queue* q : queues)
            for (auto it = q->begin(); it != q->end();) {
                if (match(*it)) {
                    if (outside)
                        gone.push_back(std::move(*it));
                    it = q->erase(it); // without `outside`: the task (and its captures) is destroyed here, locked
                    n++;
                } else
                    ++it;
            }
    }
    gone.clear(); // unlocked
    return n;
}

} // namespace sk
