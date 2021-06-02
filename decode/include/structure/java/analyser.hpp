#ifndef ANALYSER_HPP
#define ANALYSER_HPP

#include "structure/config.hpp"
#include "block.hpp"
#include "klass.hpp"

using namespace std;

class Analyser {
  friend class Matcher;
  private:
    Config _config;
    map<string, Klass *> _Ks;
    bool _is_parse_all = false;
    bool _is_analyse_all = false;
    bool _is_analyse_hierarchy = false;
    bool _is_analyse_call_graph = false;
    list<pair<int, const Method*>> all_call_sites;
    list<const Method*> all_methods;
    list<const Method*> callbacks;
    unordered_map<const Method*, int> method_map;

    void get_invoke_method(u1 bc, string signature, Method *method, int offset);
    void get_return_method(u1 bc, Method *method, int offset);

  public:
    Analyser(string configFile);
    ~Analyser();
    void parse_all();
    void analyse_hierarchy();
    void analyse_call_graph();
    void analyse_all();
    void analyse_callback(const char *name);
    const Klass *getKlass(string klassName);
    const list<const Method *> *get_all_methods() { return &all_methods; };
    const list<const Method *> *get_callbacks() { return &callbacks; }
    const list<pair<int, const Method*>> *get_all_call_sites() { return &all_call_sites; }
    const unordered_map<const Method*, int> &get_method_map() { return method_map; }
    void output_method_map();
    void output();
};

#endif // ANALYSER_HPP