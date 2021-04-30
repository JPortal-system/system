#ifndef COMMON_HPP
#define COMMON_HPP

// #define NDEBUG
#include <atomic>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace std;

enum Argv {
    ARGV_PROJECT_DIR = 0x0,
    ARGV_JAVA_LIB_DIR = 0x1,
    ARGV_AGENT_FILE = 0x2,
    ARGV_START = 0x3,
    ARGV_STOP = 0x4,

    number_of_argv
};

inline bool is_file_exist(const string &filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

#endif // COMMON_HPP