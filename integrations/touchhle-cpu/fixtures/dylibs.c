/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Execute the bundled libraries as guest ARM code, not host substitutes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int compress(unsigned char *, unsigned long *, const unsigned char *, unsigned long);
extern int uncompress(unsigned char *, unsigned long *, const unsigned char *, unsigned long);
typedef struct sqlite3 sqlite3;
extern int sqlite3_open(const char *, sqlite3 **);
extern int sqlite3_exec(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
extern int sqlite3_close(sqlite3 *);
extern void sqlite3_free(void *);
extern void *xmlReadMemory(const char *, int, const char *, const char *, int);
extern void *xmlDocGetRootElement(void *);
extern unsigned char *xmlNodeGetContent(void *);
extern void xmlFreeDoc(void *);
extern void (*xmlFree)(void *);
/* Itanium ABI names for operator new(unsigned long) and operator delete(void*). */
extern void *_Znwm(unsigned long);
extern void _ZdlPv(void *);

static unsigned failures;
#define CHECK(value) do { if (!(value)) { failures++; \
    printf("FAIL line %u: %s\n", (unsigned)__LINE__, #value); } } while (0)

static int row(void *context, int count, char **values, char **names) {
    unsigned *rows = context;
    (*rows)++;
    CHECK(count == 1 && strcmp(names[0], "value") == 0);
    CHECK(count == 1 && values[0] && strcmp(values[0], "42") == 0);
    return 0;
}

int main(void) {
    unsigned *allocation = _Znwm(sizeof(unsigned));
    CHECK(allocation != NULL);
    if (allocation) {
        *allocation = 42;
        CHECK(*allocation == 42);
        _ZdlPv(allocation);
    }
    const unsigned char plain[] = "Shared ARMv6 library round trip. Shared ARMv6 library round trip.";
    unsigned char packed[256], unpacked[256] = {0};
    unsigned long packed_size = sizeof packed, unpacked_size = sizeof unpacked;
    CHECK(compress(packed, &packed_size, plain, sizeof plain) == 0);
    CHECK(packed_size > 0 && packed_size < sizeof plain);
    CHECK(uncompress(unpacked, &unpacked_size, packed, packed_size) == 0);
    CHECK(unpacked_size == sizeof plain && memcmp(unpacked, plain, sizeof plain) == 0);

    sqlite3 *db = NULL;
    char *error = NULL;
    unsigned rows = 0;
    CHECK(sqlite3_open(":memory:", &db) == 0);
    if (db) {
        CHECK(sqlite3_exec(db, "CREATE TABLE sample(value INTEGER);"
              "INSERT INTO sample VALUES(42); SELECT value FROM sample;",
              row, &rows, &error) == 0);
        if (error) {
            printf("SQLite: %s\n", error);
            sqlite3_free(error);
        }
        CHECK(rows == 1);
        CHECK(sqlite3_close(db) == 0);
    }

    const char xml[] = "<root>Guest &amp; library</root>";
    void *doc = xmlReadMemory(xml, sizeof xml - 1, "test.xml", NULL, 0);
    CHECK(doc != NULL);
    if (doc) {
        void *root = xmlDocGetRootElement(doc);
        CHECK(root != NULL);
        if (root) {
            unsigned char *content = xmlNodeGetContent(root);
            CHECK(content && strcmp((const char *)content, "Guest & library") == 0);
            if (content) xmlFree(content);
        }
        xmlFreeDoc(doc);
    }
    printf("S5LBOX-HLE-DYLIBS %s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
