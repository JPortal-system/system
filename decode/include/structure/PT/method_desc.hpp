#ifndef METHOD_DESC_HPP
#define METHOD_DESC_HPP
#include <string>

class MethodDesc {
    int klass_name_length;
    int name_length;
    int signature_length;
    const char *klass_name;
    const char *name;
    const char *signature;

  public:  
    MethodDesc(int _klass_name_length = 0, int _name_length = 0,
                int _signature_length = 0, const char *_klass_name = nullptr,
                const char *_name = nullptr, const char *_signature = nullptr):
        klass_name_length(_klass_name_length),
        name_length(_name_length),
        signature_length(_signature_length),
        klass_name(_klass_name),
        name(_name),
        signature(_signature) {}
    void get_method_desc(std::string &_klass_name, std::string &_name, std::string &_signature) const {
      _klass_name = klass_name?std::string(klass_name, klass_name_length):"";
      _name = name?std::string(name, name_length):"";
      _signature = signature?std::string(signature, signature_length):"";
    }

};

#endif