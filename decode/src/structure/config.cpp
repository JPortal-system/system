#include "structure/config.hpp"

Config::Config(string configFile) {
    ifstream infile(configFile.c_str());
    string line;
    // read dirs
    bool isBreak = false;
    while (getline(infile, line)) {
        line = trim(line);
        if (line == "") {
            continue;
        }
        if (line != "[") {
            cout << "config file format error\n";
            exit(1);
        }
        while (getline(infile, line)) {
            line = trim(line);
            if (line == "") {
                continue;
            }
            if (line == "]") {
                isBreak = true;
                break;
            }
            classFilePaths.push_back(line);
        }
        if (isBreak)
            break;
    }
    // read options
    while (getline(infile, line)) {
        line = trim(line);
        if (line == "") {
            continue;
        }
        unordered_map<string, string> oneBlockOption;
        string optionBlockName = line;
        getline(infile, line);
        line = trim(line);
        if (line != "{") {
            cout << "config file format error\n";
            exit(1);
        }
        while (getline(infile, line)) {
            line = trim(line);
            if (line == "") {
                continue;
            }
            if (line == "}") {
                options.insert(make_pair(optionBlockName, oneBlockOption));
                break;
            }
            auto parsedLine = parseOptionLine(line);
            oneBlockOption.insert(parsedLine);
        }
    }
}

pair<string, string> Config::parseOptionLine(string optionLine) {
    int index = optionLine.find("=");
    string name = optionLine.substr(0, index);
    string value = optionLine.substr(index + 1);
    name = trim(name);
    value = trim(value);
    return make_pair(name, value);
}

unordered_map<string, string> Config::getOptionBlock(string blockName) {
    unordered_map<string, unordered_map<string, string>>::const_iterator got =
        options.find(blockName);
    if (got == options.end()) {
        cout << "block name not found\n";
        exit(1);
    } else {
        return got->second;
    }
}

unordered_map<string, unordered_map<string, string>>
Config::getAllOptionBlocks() {
    return options;
}

ostream &operator<<(ostream &os, Config &c) {
    for (auto block : c.getAllOptionBlocks()) {
        os << "block name: " << block.first << "\n";
        for (auto blockOpt : block.second) {
            os << "\t"
               << "option name = " << blockOpt.first << "\n";
            os << "\t"
               << "option value = " << blockOpt.second << "\n";
        }
    }
    return os;
}

string Config::trim(string s) {
    string result = s;
    result.erase(0, result.find_first_not_of(" \t\r\n"));
    result.erase(result.find_last_not_of(" \t\r\n") + 1);
    return result;
}
