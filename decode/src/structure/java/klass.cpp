#include "structure/java/klass.hpp"
#include <iostream>
using namespace std;
void Klass::insert_method_ref(u2 index, string name) {
    _cp_index2method_ref[index] = name;
}

void Klass::insert_method_map(Method *mptr) {
    _method_map[mptr->get_name()] = mptr;
}

const Method *Klass::getMethod(string methodName) const {
    auto iter = _method_map.find(methodName);
    if (iter != _method_map.end()) {
        return iter->second;
    }
    return nullptr;
}

string Klass::index2method(u2 index) const {
    auto iter = _cp_index2method_ref.find(index);
    if (iter != _cp_index2method_ref.end())
        return iter->second;
    else
        return "";
}

void Klass::print() const {
    cout << "Methodref:" << endl;
    for (auto method_ref : _cp_index2method_ref) {
        cout << method_ref.first << ": " << method_ref.second << endl;
    }
}