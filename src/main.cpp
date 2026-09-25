#include "version.hpp"

#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int ui_main(int argc, char** argv);

namespace {

bool command_ok(const char* name) {
    if (!name) return false;
    const char* path = ::getenv("PATH");
    std::string input = path ? path : "";
    size_t start = 0;
    while (start <= input.size()) {
        auto end = input.find(':', start);
        if (end == std::string::npos) end = input.size();
        auto dir = input.substr(start, end - start);
        if (!dir.empty() && ::access((dir + "/" + name).c_str(), X_OK) == 0) return true;
        if (end == input.size()) break;
        start = end + 1;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (::geteuid() != 0) {
        std::vector<char*> args;
        args.push_back(const_cast<char*>("pkexec"));
        for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
        args.push_back(nullptr);
        if (command_ok("pkexec")) {
            ::execvp("pkexec", args.data());
        }
        args[0] = const_cast<char*>("sudo");
        if (command_ok("sudo")) ::execvp("sudo", args.data());
        std::cerr << "Run ACS as root: sudo ACS-cpp\n";
        return 1;
    }
    return ui_main(argc, argv);
}
