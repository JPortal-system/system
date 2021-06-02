#ifndef JAVA_KLASS_HPP
#define JAVA_KLASS_HPP

#include "method.hpp"
#include <unordered_map>
#include <list>

using namespace std;

class Klass {
  private:
    string _name;
    // <ConstantPool_index, class.name+signature>
    map<u2, string> _cp_index2method_ref;
    map<string, Method *> _method_map;
    string _father_name;
    list<string> _interface_name_list;
    Klass *_father = nullptr;
    list<Klass *> _children;
  public:
    Klass(string name, bool exist) : _name(name) {}

    ~Klass() {
        for (auto i : _method_map) {
            delete (i.second);
        }
    }
    string get_name() const { return _name; }
    void insert_method_ref(u2 index, string name);
    void insert_method_map(Method *mptr);
    const Method *getMethod(string methodName) const;
    string index2method(u2 index) const;
    void print() const;
    void set_father_name(string father_name) { _father_name = father_name; }
    string get_father_name() { return _father_name; }
    void add_interface_name(string interface_name) { _interface_name_list.push_back(interface_name); }
    list<string> get_interface_name_list() { return _interface_name_list; }
    void set_father(Klass *father) { _father = father; }
    Klass *get_father() { return _father; }
    void add_child(Klass *child) { _children.push_back(child); }
    list<Klass *> get_children() { return _children; }
    map<string, Method*> *get_method_map() { return &_method_map; }
};

#endif // JAVA_KLASS_HPP