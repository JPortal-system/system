#ifndef TASK_CONFIG_HPP
#define TASK_CONFIG_HPP

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

class Config {
    friend class Analyser;
  private:
    vector<string> classFilePaths;
    unordered_map<string, unordered_map<string, string>> options;
    pair<string, string> parseOptionLine(string optionLine);
    string trim(string s);

  public:
    Config(string configFile);
    unordered_map<string, string> getOptionBlock(string blockName);
    friend ostream &operator<<(ostream &os, Config &c);
    unordered_map<string, unordered_map<string, string>> getAllOptionBlocks();
};

#endif // TASK_CONFIG_HPP
