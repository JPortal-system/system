#ifndef JIT_IMAGE_HPP
#define JIT_IMAGE_HPP

#include <stdint.h>

struct jit_section;

/* A list of sections. */
struct jit_section_list {
	/* The next list element. */
	struct jit_section_list *next;

	/* The mapped section. */
	struct jit_section *section;
};

/* A traced image consisting of a collection of sections. */
struct jit_image {
	/* The optional image name. */
	char *name;

	/* The list of sections. */
	struct jit_section_list *sections;

	/* The list of removed sections. */
	struct jit_section_list *removed;
};

/** Allocate a traced memory image.
 *
 * An optional \@name may be given to the image.  The name string is copied.
 *
 * Returns a new traced memory image on success, NULL otherwise.
 */
extern struct jit_image *jit_image_alloc(const char *name);

/** Free a traced memory image.
 *
 * The \@image must have been allocated with jit_image_alloc().
 * The \@image must not be used after a successful return.
 */
extern void jit_image_free(struct jit_image *image);

/** Get the image name.
 *
 * Returns a pointer to \@image's name or NULL if there is no name.
 */
extern const char *jit_image_name(const struct jit_image *image);

/* Initialize an image with an optional @name. */
extern void jit_image_init(struct jit_image *image, const char *name);

/* Finalize an image.
 *
 * This removes all sections and frees the name.
 */
extern void jit_image_fini(struct jit_image *image);

/* Add a section to an image.
 *
 * Add @section identified by @isid to @image at @vaddr in @asid.  If @section
 * overlaps with existing sections, the existing sections are shrunk, split, or
 * removed to accomodate @section.  Absence of a section identifier is indicated
 * by an @isid of zero.
 *
 * Returns zero on success.
 * Returns -pte_internal if @image, @section, or @asid is NULL.
 */
extern int jit_image_add(struct jit_image *image, struct jit_section *section);

/* Remove a section from an image.
 *
 * Returns zero on success.
 * Returns -pte_internal if @image, @section, or @asid is NULL.
 * Returns -pte_bad_image if @image does not contain @section at @vaddr.
 */
extern int jit_image_remove(struct jit_image *image, uint64_t vadrr);

/* Find an image section.
 *
 * Find the section containing @vaddr in @asid and provide it in @msec.  On
 * success, takes a reference of @msec->section that the caller needs to put
 * after use.
 *
 * Returns the section's identifier on success, a negative error code otherwise.
 * Returns -pte_internal if @image, @msec, or @asid is NULL.
 * Returns -pte_nomap if there is no such section in @image.
 */
extern int jit_image_find(struct jit_image *image, struct jit_section **section,
			 uint64_t vaddr);

/* Validate an image section.
 *
 * Validate that a lookup of @vaddr in @image.
 *
 * Validation may fail sporadically.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_invalid if @image or @msec is NULL.
 * Returns -pte_nomap if validation failed.
 */
extern int jit_image_validate(struct jit_image *image,
			     struct jit_section *section,
			     uint64_t vaddr);

#endif