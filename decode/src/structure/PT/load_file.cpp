#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int extract_base(char *arg, uint64_t *base) {
    char *sep, *rest;

    sep = strrchr(arg, ':');
    if (sep) {
        uint64_t num;

        if (!sep[1])
            return 0;

        errno = 0;
        num = strtoull(sep+1, &rest, 0);
        if (errno || *rest)
            return 0;

        *base = num;
        *sep = 0;
        return 1;
    }

    return 0;
}

static int parse_range(const char *arg, uint64_t *begin, uint64_t *end) {
    char *rest;

    if (!arg || !*arg)
        return 0;

    errno = 0;
    *begin = strtoull(arg, &rest, 0);
    if (errno)
        return -1;

    if (!*rest)
        return 1;

    if (*rest != '-')
        return -1;

    *end = strtoull(rest+1, &rest, 0);
    if (errno || *rest)
        return -1;

    return 2;
}

/* Preprocess a filename argument.
 *
 * A filename may optionally be followed by a file offset or a file range
 * argument separated by ':'.  Split the original argument into the filename
 * part and the offset/range part.
 *
 * If no end address is specified, set @size to zero.
 * If no offset is specified, set @offset to zero.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int preprocess_filename(char *filename, uint64_t *offset, uint64_t *size) {
    uint64_t begin, end;
    char *range;
    int parts;

    if (!filename || !offset || !size)
        return -1;

    /* Search from the end as the filename may also contain ':'. */
    range = strrchr(filename, ':');
    if (!range) {
        *offset = 0ull;
        *size = 0ull;

        return 0;
    }

    /* Let's try to parse an optional range suffix.
     *
     * If we can, remove it from the filename argument.
     * If we can not, assume that the ':' is part of the filename, e.g. a
     * drive letter on Windows.
     */
    parts = parse_range(range + 1, &begin, &end);
    if (parts <= 0) {
        *offset = 0ull;
        *size = 0ull;

        return 0;
    }

    if (parts == 1) {
        *offset = begin;
        *size = 0ull;

        *range = 0;

        return 0;
    }

    if (parts == 2) {
        if (end <= begin)
            return -1;

        *offset = begin;
        *size = end - begin;

        *range = 0;

        return 0;
    }

    return -1;
}

int load_file(uint8_t **buffer, size_t *psize, const char *filename,
             uint64_t offset, uint64_t size, const char *prog) {
    uint8_t *content;
    size_t read;
    FILE *file;
    long fsize, begin, end;
    int errcode;

    if (!buffer || !psize || !filename || !prog) {
        fprintf(stderr, "%s: internal error.\n", prog ? prog : "");
        return -1;
    }

    errno = 0;
    file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "%s: failed to open %s: %d.\n",
            prog, filename, errno);
        return -1;
    }

    errcode = fseek(file, 0, SEEK_END);
    if (errcode) {
        fprintf(stderr, "%s: failed to determine size of %s: %d.\n",
            prog, filename, errno);
        goto err_file;
    }

    fsize = ftell(file);
    if (fsize < 0) {
        fprintf(stderr, "%s: failed to determine size of %s: %d.\n",
            prog, filename, errno);
        goto err_file;
    }

    begin = (long) offset;
    if (((uint64_t) begin != offset) || (fsize <= begin)) {
        fprintf(stderr,
            "%s: bad offset 0x%" PRIx64 " into %s.\n",
            prog, offset, filename);
        goto err_file;
    }

    end = fsize;
    if (size) {
        uint64_t range_end;

        range_end = offset + size;
        if ((uint64_t) end < range_end) {
            fprintf(stderr,
                "%s: bad range 0x%" PRIx64 " in %s.\n",
            prog, range_end, filename);
            goto err_file;
        }

        end = (long) range_end;
    }

    fsize = end - begin;

    content = (uint8_t *)malloc((size_t) fsize);
    if (!content) {
        fprintf(stderr, "%s: failed to allocated memory %s.\n",
            prog, filename);
        goto err_file;
    }

    errcode = fseek(file, begin, SEEK_SET);
    if (errcode) {
        fprintf(stderr, "%s: failed to load %s: %d.\n",
            prog, filename, errno);
        goto err_content;
    }

    read = fread(content, (size_t) fsize, 1u, file);
    if (read != 1) {
        fprintf(stderr, "%s: failed to load %s: %d.\n",
            prog, filename, errno);
        goto err_content;
    }

    fclose(file);

    *buffer = content;
    *psize = (size_t) fsize;

    return 0;

err_content:
    free(content);

err_file:
    fclose(file);
    return -1;
}