#ifndef LOAD_FILE_HPP
#define LOAD_FILE_HPP

#include <stdint.h>
#include <stdio.h>

extern int preprocess_filename(char *filename, uint64_t *offset, uint64_t *size);

extern int load_file(uint8_t **buffer, size_t *psize, const char *filename,
		     uint64_t offset, uint64_t size, const char *prog);

#endif