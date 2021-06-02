#ifndef JIT_SECTION_HPP
#define JIT_SECTION_HPP

#include <stdint.h>
#include <string>
#include <unordered_map>
#include "threads.hpp"
#include "structure/PT/compiled_method_desc.hpp"
#include "structure/PT/method_desc.hpp"
#include "structure/java/type_defs.hpp"
 
using namespace std;

/*
 * Record that gives information about the methods on the compile-time
 * stack at a specific pc address of a compiled method. Each element in
 * the methods array maps to same element in the bcis array.
 */
typedef struct _PCStackInfo {
  	uint64_t pc;             /* the pc address for this compiled method */
  	jint numstackframes;  /* number of methods on the stack */
  	jint* methods;   /* array of numstackframes method ids */
  	jint* bcis;           /* array of numstackframes bytecode indices */
} PCStackInfo;

/*
 * Record that contains inlining information for each pc address of
 * an nmethod.
 */
typedef struct _CompiledMethodLoadInlineRecord {
  	jint numpcs;          /* number of pc descriptors in this nmethod */
  	PCStackInfo* pcinfo;  /* array of numpcs pc descriptors */
} CompiledMethodLoadInlineRecord;

/* A section of contiguous memory loaded from a file. */
struct jit_section {
	/* description of jit codes */
	const uint8_t *code;

	uint64_t code_begin;

	uint64_t code_size;

	const CompiledMethodDesc *cmd;
	CompiledMethodLoadInlineRecord *record;

	const char *name;

	/* A lock protecting this section.
	 *
	 * Most operations do not require the section to be locked.  All
	 * actual locking should be handled by pt_section_* functions.
	 */
	mtx_t lock;

	/* A lock protecting the @iscache and @acount fields.
	 *
	 * We need separate locks to protect against a deadlock scenario when
	 * the iscache is mapping or unmapping this section.
	 *
	 * The attach lock must not be taken while holding the section lock; the
	 * other way round is OK.
	 */
	mtx_t alock;

    /* The number of current users.  The last user destroys the section. */
	uint16_t ucount;

	/* The number of attaches.  This must be <= @ucount. */
	uint16_t acount;

	/* The number of current mappers.  The last unmaps the section. */
	uint16_t mcount;
};

/* Create a section.
 *
 * The returned section describes the contents of @file starting at @offset
 * for @size bytes.
 *
 *
 * The returned section is not mapped and starts with a user count of one and
 * instruction caching enabled.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 * Returns -pte_internal if @psection is NULL.
 * Returns -pte_nomem when running out of memory.
 * Returns -pte_bad_file if @filename cannot be opened.
 * Returns -pte_invalid if @offset lies beyond @file.
 * Returns -pte_invalid if @filename is too long.
 */
extern int jit_mk_section(struct jit_section **psection,
                        const uint8_t *code,
                        uint64_t code_begin, uint64_t code_size, 
                        const uint8_t *scopes_pc,
                        const uint8_t *scopes_data,
                        CompiledMethodDesc *cmd,
						const char *name);

/* Lock a section.
 *
 * Locks @section.  The section must not be locked.
 *
 * Returns a new section on success, NULL otherwise.
 * Returns -pte_bad_lock on any locking error.
 */
extern int jit_section_lock(struct jit_section *section);

/* Unlock a section.
 *
 * Unlocks @section.  The section must be locked.
 *
 * Returns a new section on success, NULL otherwise.
 * Returns -pte_bad_lock on any locking error.
 */
extern int jit_section_unlock(struct jit_section *section);

/* free a section */
extern void jit_section_free(struct jit_section *section);

/* Add another user.
 *
 * Increments the user count of @section.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @section is NULL.
 * Returns -pte_overflow if the user count would overflow.
 * Returns -pte_bad_lock on any locking error.
 */
extern int jit_section_get(struct jit_section *section);

/* Remove a user.
 *
 * Decrements the user count of @section.  Destroys the section if the
 * count reaches zero.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @section is NULL.
 * Returns -pte_internal if the user count is already zero.
 * Returns -pte_bad_lock on any locking error.
 */
extern int jit_section_put(struct jit_section *section);

extern const CompiledMethodDesc *jit_section_cmd(const struct jit_section *section);

/* Return the size of the section in bytes. */
extern uint64_t jit_section_code_size(const struct jit_section *section);

/* Return the address of the section where the instruction begins. */
extern uint64_t jit_section_code_begin(const struct jit_section *section);

/* Read memory from a section.
 *
 * Reads at most @size bytes from @section with @vaddr in @buffer.  @section
 * must be mapped.
 *
 * Returns the number of bytes read on success, a negative error code otherwise.
 * Returns -pte_internal if @section or @buffer are NULL.
 * Returns -pte_nomap if @offset is beyond the end of the section.
 */
extern int jit_section_read(struct jit_section *section, uint8_t *buffer,
			   uint16_t size, uint64_t vaddr);

/* read jitted code debug info */
extern int jit_section_read_debug_info(struct jit_section *section,
								uint64_t vaddr, PCStackInfo *& pcinfo);

#endif