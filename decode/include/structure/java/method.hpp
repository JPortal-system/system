#ifndef JAVA_METHOD_HPP
#define JAVA_METHOD_HPP

#include "block.hpp"
#include <list>

class Klass;
class Method {
  private:
    const string _name_signature; // name+signature
    BlockGraph *_bg;
    map<int, list<const Method *>> _callee_map;
    list<pair<int, const Method *>> _caller_list;
    const Klass &_klass;
  public:
    Method(string name_signatiue, const u1 *const code_start,
           const u2 code_length, const u1 *const exception_table,
           const u2 exception_table_length, const Klass &klass);
    ~Method() { delete _bg; }
    string get_name() const { return _name_signature; }
    BlockGraph *get_bg() const { return _bg; }
    void add_callee_list(int offset, list<const Method *> call_list) {
      _callee_map[offset] = call_list;
    }
    void add_caller(int offset, const Method *method) {
      _caller_list.push_back(make_pair(offset, method));
    }
    const map<int, list<const Method *>> &get_callee_map() const {
      return _callee_map;
    }
    const list<const Method *> *get_callee_list(int offset) const {
      auto cm = _callee_map.find(offset);
      if (cm == _callee_map.end())
        return nullptr;
      return &((*cm).second);
    };
    const list<pair<int, const Method*>> *get_caller_list() const {
      return &_caller_list;
    }
    const Klass *get_klass() const { return &_klass; }
    void print_graph() const;
    void print_bctlist() const;
};
#endif // JAVA_METHOD_HPP