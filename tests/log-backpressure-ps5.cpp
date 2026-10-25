// Compile-only unless explicitly sent to elfldr by the console operator.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PS5LM_LOG_TEST_RESULT_PATH
#define PS5LM_LOG_TEST_RESULT_PATH "/data/PS5LM/log-backpressure-test.jsonl"
#endif
#define PS5LM_LOG_TEST_NO_MAIN
#include "log-backpressure.cpp"
#include <condition_variable>
int main() {
    FILE * result = fopen(PS5LM_LOG_TEST_RESULT_PATH, "wx");
    if (!result) std::_Exit(2);
    fprintf(result, "{\"stage\":\"started\"}\n");
    fflush(result); fsync(fileno(result));
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(m);
        if (!cv.wait_for(lock, std::chrono::seconds(20), [&] { return done; })) {
            fprintf(result, "{\"stage\":\"timeout\",\"ok\":false}\n");
            fflush(result); fsync(fileno(result));
            std::_Exit(124);
        }
    });
    int text = 0, text_count = 0;
    for (; text_count < 25 && text == 0; ++text_count) text = run_log_backpressure_test(false);
    fprintf(result, "{\"stage\":\"text\",\"iterations\":%d,\"exit_code\":%d}\n", text_count, text);
    fflush(result); fsync(fileno(result));
    int json = 0, json_count = 0;
    for (; json_count < 25 && json == 0; ++json_count) json = run_log_backpressure_test(true);
    fprintf(result, "{\"stage\":\"json\",\"iterations\":%d,\"exit_code\":%d}\n", json_count, json);
    fprintf(result, "{\"stage\":\"complete\",\"ok\":%s}\n", text == 0 && json == 0 ? "true" : "false");
    fflush(result); fsync(fileno(result));
    { std::lock_guard<std::mutex> lock(m); done = true; }
    cv.notify_one(); watchdog.join(); fclose(result);
    // Payload SDK teardown may destroy C++ runtime state still used by threads.
    std::_Exit(text == 0 && json == 0 ? 0 : 1);
}
