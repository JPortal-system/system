#ifndef COMPILED_METHOD_DESC_HPP
#define COMPILED_METHOD_DESC_HPP

#include <stdint.h>
#include <map>
#include "structure/PT/method_desc.hpp"
#include "structure/java/type_defs.hpp"

using namespace std;

class CompiledMethodDesc {
  private:
    uint64_t scopes_pc_size;
  	uint64_t scopes_data_size;
  	uint64_t entry_point;
  	uint64_t verified_entry_point;
  	uint64_t osr_entry_point;
  	int inline_method_cnt;

  	MethodDesc main_md;

  	map<int, MethodDesc> method_desc;

  public:
	CompiledMethodDesc() {}

  	CompiledMethodDesc(uint64_t _scopes_pc_size, uint64_t _scopes_data_size,
                      uint64_t _entry_point, uint64_t _verified_entry_point,
                      uint64_t _osr_entry_point, int _inline_method_cnt,
					  MethodDesc _main_md,
					  map<int, MethodDesc> _method_desc) :
    	scopes_pc_size(_scopes_pc_size),
    	scopes_data_size(_scopes_data_size),
    	entry_point(_entry_point),
    	verified_entry_point(_verified_entry_point),
    	osr_entry_point(_osr_entry_point),
    	inline_method_cnt(_inline_method_cnt),
		main_md(_main_md), method_desc(_method_desc) {}

	void get_main_method_desc (string &klass_name, string &name, string &sig) const {
		return main_md.get_method_desc(klass_name, name, sig);
	}
	bool get_method_desc(int id, string &klass_name, string &name, string &sig) const {
		auto iter = method_desc.find(id);
		if (iter == method_desc.end())
			return false;
		iter->second.get_method_desc(klass_name, name, sig);
		return true;
	}
	const map<int, MethodDesc> &get_method_desc() const { return method_desc; }
	uint64_t get_scopes_pc_size() const { return scopes_pc_size; }
  	uint64_t get_scopes_data_size() const { return scopes_data_size; }
  	uint64_t get_entry_point() const { return entry_point; }
  	uint64_t get_verified_entry_point() const { return verified_entry_point; }
  	uint64_t get_osr_entry_point() const { return osr_entry_point; }
  	int get_inline_method_cnt() const { return inline_method_cnt; }
};

#endif