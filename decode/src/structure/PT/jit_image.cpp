#include "structure/PT/jit_section.hpp"
#include "structure/PT/jit_image.hpp"

#include <stdlib.h>
#include <string.h>
#include <intel-pt.h>

static char *dupstr(const char *str)
{
	char *dup;
	size_t len;

	if (!str)
		return NULL;

	/* Silently truncate the name if it gets too big. */
	len = strnlen(str, 4096ul);

	dup = (char *)malloc(len + 1);
	if (!dup)
		return NULL;

	dup[len] = 0;

	return (char *)memcpy(dup, str, len);
}

static struct jit_section_list *jit_mk_section_list(struct jit_section *section)
{
	struct jit_section_list *list;
	int errcode;

	list = (struct jit_section_list *)malloc(sizeof(*list));
	if (!list)
		return NULL;

	memset(list, 0, sizeof(*list));

	errcode = jit_section_get(section);
	if (errcode < 0)
		goto out_mem;

	list->section = section;

	return list;

out_mem:
	free(list);
	return NULL;
}

static void jit_section_list_free(struct jit_section_list *list)
{
	if (!list)
		return;

	jit_section_put(list->section);
	free(list);
}

static void jit_section_list_free_tail(struct jit_section_list *list)
{
	while (list) {
		struct jit_section_list *trash;

		trash = list;
		list = list->next;

		jit_section_list_free(trash);
	}
}

void jit_image_init(struct jit_image *image, const char *name)
{
	if (!image)
		return;

	memset(image, 0, sizeof(*image));

	image->name = dupstr(name);
}

void jit_image_fini(struct jit_image *image)
{
	if (!image)
		return;

	jit_section_list_free_tail(image->sections);
	jit_section_list_free_tail(image->removed);
	free(image->name);

	memset(image, 0, sizeof(*image));
}

struct jit_image *jit_image_alloc(const char *name)
{
	struct jit_image *image;

	image = (struct jit_image *)malloc(sizeof(*image));
	if (image)
		jit_image_init(image, name);

	return image;
}

void jit_image_free(struct jit_image *image)
{
	jit_image_fini(image);
	free(image);
}

const char *jit_image_name(const struct jit_image *image)
{
	if (!image)
		return NULL;

	return image->name;
}

int jit_image_add(struct jit_image *image, struct jit_section *section)
{
	struct jit_section_list **list, *next, *removed;
	uint64_t size, begin, end;

	if (!image || !section)
		return -pte_internal;

	size = jit_section_code_size(section);
	begin = jit_section_code_begin(section);
	end = begin + size;

	next = jit_mk_section_list(section);
	if (!next)
		return -pte_nomem;

	/* Check for overlaps while we move to the end of the list. */
	list = &(image->sections);
	while (*list) {
		struct jit_section_list *current;
		struct jit_section *lsec;
		uint64_t lbegin, lend, loff;

		current = *list;
		lsec = current->section;

		lbegin = jit_section_code_begin(lsec);
		lend = lbegin + jit_section_code_size(lsec);

		if ((end <= lbegin) || (lend <= begin)) {
			list = &((*list)->next);
			continue;
		}

		/* The new section overlaps with @msec's section. */

		/* We remove @sec and insert new sections for the remaining
		 * parts, if any.  Those new sections are not mapped initially
		 * and need to be added to the end of the section list.
		 */
		*list = current->next;

		/* Keep a list of removed sections so we can re-add them in case
		 * of errors.
		 */
		current->next = image->removed;
		image->removed = current;
	}

	*list = next;
	return 0;
}

int jit_image_remove(struct jit_image *image, uint64_t vadrr)
{
	struct jit_section_list **list;

	if (!image)
		return -pte_internal;

	for (list = &image->sections; *list; list = &((*list)->next)) {
		struct jit_section *sec;
		struct jit_section_list *trash;
		uint64_t begin;
		int errcode;

		trash = *list;

		sec = (*list)->section;
		if (jit_section_code_begin(sec) == vadrr) {
			*list = trash->next;
			trash->next = image->removed;
            image->removed = trash;
			return 0;
		}
	}

	return -pte_bad_image;
}

/* Check whether a section contains an address.
 *
 * Returns zero if @msec contains @vaddr.
 * Returns a negative error code otherwise.
 * Returns -pte_nomap if @msec does not contain @vaddr.
 */
static inline int jit_image_check_section(struct jit_section *section,
				      uint64_t vaddr)
{
	uint64_t begin, size, end;

	if (!section)
		return -pte_internal;

	begin = jit_section_code_begin(section);
	size = jit_section_code_size(section);
	end = begin+size;
	if (vaddr < begin || end <= vaddr)
		return -pte_nomap;

	return 0;
}

/* Find the section containing a given address in a given address space.
 *
 * On success, the found section is moved to the front of the section list.
 * If caching is enabled, maps the section.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int jit_image_fetch_section(struct jit_image *image,
				  uint64_t vaddr)
{
	struct jit_section_list **start, **list;

	if (!image)
		return -pte_internal;

	start = &image->sections;
	for (list = start; *list;) {
		struct jit_section_list *elem;
		struct jit_section *sec;
		int errcode;

		elem = *list;
		sec = elem->section;
		errcode = jit_image_check_section(sec, vaddr);
		if (errcode < 0) {
			if (errcode != -pte_nomap)
				return errcode;

			list = &elem->next;
			continue;
		}

		/* Move the section to the front if it isn't already. */
		if (list != start) {
			*list = elem->next;
			elem->next = *start;
			*start = elem;
		}

		return 0;
	}

	return -pte_nomap;
}

int jit_image_find(struct jit_image *image, struct jit_section **psection,
			uint64_t vaddr)
{
	struct jit_section_list *slist;
	struct jit_section *section;
	int errcode;

	if (!image || !psection)
		return -pte_internal;

	errcode = jit_image_fetch_section(image, vaddr);
	if (errcode < 0)
		return errcode;

	slist = image->sections;
	if (!slist)
		return -pte_internal;

	section = slist->section;

	errcode = jit_section_get(section);
	if (errcode < 0)
		return errcode;

	*psection = section;

	return 0;
}

int jit_image_validate(struct jit_image *image,
		      struct jit_section *section, uint64_t vaddr)
{
	const struct jit_section_list *slist;
	uint64_t begin, size, end;
	int status;

	if (!image || !section)
		return -pte_internal;

	/* Check that @vaddr lies within @usec. */
	begin = jit_section_code_begin(section);
	size = jit_section_code_size(section);
	end = begin + size;
	if (vaddr < begin || end <= vaddr)
		return -pte_nomap;

	/* We assume that @usec is a copy of the top of our stack and accept
	 * sporadic validation fails if it isn't, e.g. because it has moved
	 * down.
	 *
	 * A failed validation requires decoders to re-fetch the section so it
	 * only results in a (relatively small) performance loss.
	 */
	slist = image->sections;
	if (!slist)
		return -pte_nomap;
	
	if (slist->section != section)
		return -pte_nomap;

	return 0;
}