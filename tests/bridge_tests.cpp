#include "bridge.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

int main() {
    try {
        ck::JobDirectory tmp(std::filesystem::temp_directory_path() / "ck-bridge-test");
        auto script = tmp.path / "fixture.py";
        std::ofstream(script) << "import json,sys,time\n"
            "for line in sys.stdin:\n"
            " j=json.loads(line)\n"
            " if j.get('sleep'): time.sleep(30)\n"
            " if j.get('exit'): sys.exit(2)\n"
            " if j.get('progress'):\n"
            "  sys.stdout.write('{\"event\":\"progress\",\"fraction\":0.25,')\n"
            "  sys.stdout.flush()\n"
            "  time.sleep(.02)\n"
            "  sys.stdout.write('\"message\":\"Quarter\"}\\n{\"event\":\"progress\",\"fraction\":0.75,\"message\":\"Three quarters\"}\\n')\n"
            " print(json.dumps({'ok':True,'value':j.get('value')}),flush=True)\n";
        ck::Runtime runtime{"/usr/bin/python", script.string(), tmp.path.string(), tmp.path.string()};
        ck::Worker worker;
        auto reply = worker.request(runtime, {{"value", "quotes ' and spaces"}}, [] { return false; }, 3);
        if (reply.at("value") != "quotes ' and spaces") throw std::runtime_error("JSON round trip");
        bool cancelled = false;
        const auto before = std::chrono::steady_clock::now();
        try { worker.request(runtime, {{"sleep", true}}, [] { return true; }, 3); }
        catch (const std::exception&) { cancelled = true; }
        if (!cancelled || std::chrono::steady_clock::now() - before > std::chrono::seconds(3))
            throw std::runtime_error("Cancellation must be bounded");
        bool died = false;
        try { worker.request(runtime, {{"exit", true}}, [] { return false; }, 3); }
        catch (const std::exception&) { died = true; }
        if (!died) throw std::runtime_error("Worker death must surface");
        worker.request(runtime, {{"value", 42}}, [] { return false; }, 3);
        std::vector<double> progress;
        reply = worker.request(runtime, {{"value", 84}, {"progress", true}}, [] { return false; }, 3,
            [&](double fraction, const std::string&) { progress.push_back(fraction); });
        if (progress != std::vector<double>{.25, .75} || reply.at("value") != 84)
            throw std::runtime_error("Partial and coalesced progress messages must precede final response");
        bool progressCancelled = false;
        try {
            worker.request(runtime, {{"progress", true}}, [] { return false; }, 3,
                [](double, const std::string&) { throw std::runtime_error("host cancelled"); });
        } catch (const std::exception&) { progressCancelled = true; }
        if (!progressCancelled) throw std::runtime_error("Progress cancellation must stop worker");
        worker.request(runtime, {{"value", 85}}, [] { return false; }, 3);
        std::cout << "Worker lifecycle contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
