#include "structure/java/analyser.hpp"
#include "structure/java/classFileParser.hpp"
#include "structure/common.hpp"
#include "structure/PT/codelets_entry.hpp"
#include <dirent.h>

Analyser::Analyser(string configFile) : _config(configFile) {}
Analyser::~Analyser() {
    for (auto k : _Ks) {
        delete (k.second);
    }
}

const Klass *Analyser::getKlass(string klassName) {
    auto iter = _Ks.find(klassName);
    if (iter != _Ks.end()) {
        return iter->second;
    } else if (!_is_parse_all) {
        for (auto path : _config.classFilePaths) {
            string file_path = path + "/" + klassName + ".class";
            if (is_file_exist(file_path)) {
                _Ks[klassName] = new Klass(klassName, true);
                ClassFileParser cfp(file_path, *(_Ks[klassName]));
                cfp.parse_class();
                return _Ks[klassName];
            }
        }
    }
    return nullptr;
}

void Analyser::parse_all() {
    if (_is_parse_all)
        return;

    for (auto main_path : _config.classFilePaths) {
        struct dirent *d_ent = nullptr;
        list<string> packages;
        packages.push_back("");
        if (main_path[main_path.length() - 1] != '/')
            main_path = main_path + "/";
        while(!packages.empty()) {
            string package_name = packages.front();
            packages.pop_front();
            string path = main_path + package_name;
            DIR *dir = opendir(path.c_str());
            if (!dir) {
                cout << path << " is not a directory or not exist!" << endl;
                return;
            }

            while ((d_ent = readdir(dir)) != NULL) {
                if ((strcmp(d_ent->d_name, ".") == 0) ||
                        (strcmp(d_ent->d_name, "..") == 0))
                    continue;
                if (d_ent->d_type == DT_DIR) {
                    string sub_package = package_name + string(d_ent->d_name) + "/";
                    packages.push_back(sub_package);
                } else {
                    string name(d_ent->d_name);
                    if (name.length() > 6 &&
                            0 == name.compare(name.length() - 6, 6, ".class")) {
                        string file_path = main_path+ package_name + name;
                        // cout << "parse: " << file_path << endl;
                        string klass_name = package_name + name.substr(0, name.length()-6);
                        auto ks = _Ks.find(klass_name);
                        if (ks == _Ks.end()) {
                            _Ks[klass_name] = new Klass(klass_name, true);
                            ClassFileParser cfp(file_path, *(_Ks[klass_name]));
                            cfp.parse_class();
                        }
                    }
                }
            }
            closedir(dir);
        }
    }
    
    _is_parse_all = true;
}

void Analyser::analyse_hierarchy() {
    if (_is_analyse_hierarchy)
        return;
    if (!_is_parse_all)
        parse_all();
    
    for (auto ks : _Ks) {
        Klass *klass = ks.second;
        string father_name = klass->get_father_name();
        auto fks = _Ks.find(father_name);
        if (fks != _Ks.end()) {
            Klass *father = (*fks).second;
            klass->set_father(father);
            father->add_child(klass);
        } else if (father_name != "") {
            Klass *father = new Klass(father_name, false);
            _Ks[father_name] = father;
            klass->set_father(father);
            father->add_child(klass);
        }
        list<string> interface_name_list = klass->get_interface_name_list();
        for (auto interface_name : interface_name_list) {
            auto iks = _Ks.find(interface_name);
            if (iks == _Ks.end()) {
                Klass *interface = new Klass(interface_name, false);
                _Ks[interface_name] = interface;
                interface->add_child(klass);
            } else {
                Klass *interface = (*iks).second;
                interface->add_child(klass);
            }
        }
    }
    
    _is_analyse_hierarchy = true;
}

void Analyser::get_invoke_method(u1 bc, string signature, Method *method, int offset) {
    Bytecodes::Code code = Bytecodes::cast(bc);
    list<const Method *> call_list;
    if (code == Bytecodes::_invokedynamic) {
        for (auto ks : _Ks) {
            auto cmethod_map = (ks.second)->get_method_map();
            auto cmm = cmethod_map->find(signature);
            if (cmm == cmethod_map->end())
                continue;
            Method *cmethod = (*cmm).second;
            cmethod->add_caller(offset, method);
            call_list.push_back(cmethod);
        }
        method->add_callee_list(offset, call_list);
        return;
    }

    int index = signature.find('.');
    if (index == string::npos)
        return;
    string klass_name = signature.substr(0, index);
    string method_name = signature.substr(index+1, signature.size()-index-1);
    auto cks = _Ks.find(klass_name);
    if (cks == _Ks.end())
        return;
    Klass *cklass = (*cks).second;
    if (code == Bytecodes::_invokestatic || code == Bytecodes::_invokespecial) {
        auto cmethod_map = cklass->get_method_map();
        auto cmm = cmethod_map->find(method_name);
        if (cmm == cmethod_map->end())
            return;
        Method *cmethod = (*cmm).second;
        call_list.push_back(cmethod);
        cmethod->add_caller(offset, method);
        method->add_callee_list(offset, call_list);
    } else if (code == Bytecodes::_invokevirtual || code == Bytecodes::_invokeinterface) {
        Klass *father = cklass;
        while (father) {
            auto cmethod_map = father->get_method_map();
            auto cmm = cmethod_map->find(method_name);
            if (cmm == cmethod_map->end()) {
                father = father->get_father();
                continue;
            }
            Method *cmethod = (*cmm).second;
            call_list.push_back(cmethod);
            cmethod->add_caller(offset, method);
            father = father->get_father();
            break;
        }
        list<Klass *> children = cklass->get_children();
        while (!children.empty()) {
            Klass *child = children.front();
            children.pop_front();
            list<Klass *> cchildren = child->get_children();
            children.insert(children.end(), cchildren.begin(), cchildren.end());
            auto cmethod_map = child->get_method_map();
            auto cmm = cmethod_map->find(method_name);
            if (cmm == cmethod_map->end())
                continue;
            Method *cmethod = (*cmm).second;
            cmethod->add_caller(offset, method);
            call_list.push_back(cmethod);
        }
        method->add_callee_list(offset, call_list);
    }
}

void Analyser::analyse_call_graph() {
    if (_is_analyse_call_graph)
        return;
    if (!_is_parse_all)
        parse_all();
    if (!_is_analyse_hierarchy)
        analyse_hierarchy();
    
    for (auto ks : _Ks) {
        Klass *klass = ks.second;
        map<string, Method*> *method_map = klass->get_method_map();
        for (auto mm : *method_map) {
            Method *method = mm.second;
            BlockGraph *bg = method->get_bg();
            bg->build_graph();
            bg->build_bctlist();
            unordered_map<int, pair<u1, u2>> *call_site = bg->get_method_site();
            for (auto call : *call_site) {
                int offset = call.first;
                u1 bc = call.second.first;
                string signature = klass->index2method(call.second.second);
                get_invoke_method(bc, signature, method, offset);
            }
        }
    }

    for (auto ks : _Ks) {
        for (auto mm : *(ks.second->get_method_map())) {
                all_methods.push_back(mm.second);
        }
    }

    for (auto ks : _Ks) {
        Klass *klass = ks.second;
        map<string, Method*> *method_map = klass->get_method_map();
        for (auto mm : *method_map) {
            Method *method = mm.second;
            BlockGraph *bg = method->get_bg();
            bg->build_graph();
            bg->build_bctlist();
            unordered_map<int, pair<u1, u2>> *call_site = bg->get_method_site();
            for (auto call : *call_site) {
                int offset = call.first;
                u1 bc = call.second.first;
                all_call_sites.push_back(make_pair(offset, method));
            }
        }
    }

    _is_analyse_call_graph = true;
}

void Analyser::analyse_all() {
    if (_is_analyse_all)
        return;

    if (!_is_parse_all)
        parse_all();

    if (!_is_analyse_hierarchy)
        analyse_hierarchy();

    if (!_is_analyse_call_graph)
        analyse_call_graph();

    int method_index = 256;
    for (auto method : all_methods) {
        method_map[method] = method_index++;
    }
    callbacks = all_methods;

    _is_analyse_all = true;
}

void Analyser::analyse_callback(const char *callback) {
    FILE *fp = fopen(callback, "r");
    if (!fp) {
        fprintf(stderr, "Analyser: call back file cannot be opened.\n");
        return;
    }
    callbacks.clear();
    char klass_name[1024], name[1024];
    while (fscanf(fp, "%s %s", klass_name, name) == 2) {
        const Klass *klass = getKlass(klass_name);
        if (!klass) {
            fprintf(stderr, "Analyser: no klass named %s.\n", klass_name);
            continue;
        }
        const Method *method = klass->getMethod(name);
        if (!method) {
            fprintf(stderr, "Analyser: klass %s has no method %s.\n", klass_name, name);
            continue;
        }
        callbacks.push_back(method);
    }
}

void Analyser::output_method_map() {
    FILE *fp = fopen("methods", "w");
    for (auto method : all_methods) {
        fprintf(fp, "%s %s : %d\n", method->get_klass()->get_name().c_str(),
                    method->get_name().c_str(), method_map[method]);
    }
    fclose(fp);
}

void Analyser::output() {
    if (!_is_analyse_all)
        analyse_all();

    FILE *fp = fopen("call_graph", "w");
    if (!fp) {
        fprintf(stderr, "Analyser: fail to open file call_graph.\n");
        return;
    }
    for (auto method : all_methods) {
        fprintf(fp, "%s %s\n", method->get_klass()->get_name().c_str(),
                    method->get_name().c_str());
        for (auto call_site : method->get_callee_map()) {
            fprintf(fp, "\t%d\n", call_site.first);
            for (auto callee : call_site.second) {
                fprintf(fp, "\t\t%s %s\n", callee->get_klass()->get_name().c_str(),
                                        callee->get_name().c_str());
            }
        }
    }
    fclose(fp);
}