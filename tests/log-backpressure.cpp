#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <future>
#include <iostream>
#include <string>
#include "log.cpp"
// Isolate the logger; no terminal probing or ggml execution is needed here.
bool tty_enable_ansi() { return false; }
bool tty_can_use_colors() { return false; }
extern "C" void ggml_abort(const char *, int, const char *, ...) { std::abort(); }
int run_log_backpressure_test(bool json) {
    int sink[2];
    if (pipe(sink)) return 2;
    const int saved = dup(STDERR_FILENO);
    const int saved_out = dup(STDOUT_FILENO);
    // Fill the pipe before starting the writer, then make it blocking again.
    const int flags = fcntl(sink[1], F_GETFL);
    fcntl(sink[1], F_SETFL, flags | O_NONBLOCK);
    std::string filler(4096, 'x');
    while (write(sink[1], filler.data(), filler.size()) > 0) {}
    if (errno != EAGAIN) return 3;
    fcntl(sink[1], F_SETFL, flags);
    dup2(sink[1], STDERR_FILENO);
    dup2(sink[1], STDOUT_FILENO);
    close(sink[1]);
    std::string captured;
    bool completed;
    std::thread reader;
    {
        common_log log(8);
        common_log_set_jsonl(json);
        auto producer = std::async(std::launch::async, [&]() {
            for (int i = 0; i < 1000; ++i) {
                if (json) log.add_json("test", {{"index", i}});
                else common_log_add(&log, GGML_LOG_LEVEL_INFO, "entry %d\n", i);
            }
        });
        completed = producer.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
        reader = std::thread([&]() {
            char buf[4096];
            ssize_t n;
            while ((n = read(sink[0], buf, sizeof(buf))) > 0) captured.append(buf, n);
        });
        producer.get();
        // Let the writer drain before inserting the shutdown sentinel.
        common_log_flush(&log);
    }
    fflush(stderr);
    fflush(stdout);
    dup2(saved, STDERR_FILENO);
    close(saved);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);
    reader.join();
    close(sink[0]);
#if defined(__PROSPERO__)
    const bool pass = completed && captured.find("log lines dropped") != std::string::npos;
#else
    const bool pass = !completed && captured.find("log lines dropped") == std::string::npos
        && captured.find("999") != std::string::npos;
#endif
    std::cout << (pass ? "PASS" : "FAIL") << (json ? " JSON" : " text")
              << " producer_completed_while_sink_blocked=" << completed << std::endl;
    return pass ? 0 : 1;
}

#ifndef PS5LM_LOG_TEST_NO_MAIN
int main(int argc, char ** argv) {
    const bool json = argc > 1 && std::string(argv[1]) == "json";
    const int count = (argc > 1 && std::string(argv[argc - 1]) == "repeat") ? 50 : 1;
    for (int i = 0; i < count; ++i) {
        if (run_log_backpressure_test(json) != 0) return 1;
    }
    return 0;
}
#endif
