#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "registry.h"

/* Registry files are comma-separated, one record per line; bad lines are reported and skipped. */

#define LINE_SIZE 512

/* Strips surrounding whitespace, including a Windows '\r'. */
static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return text;
}

/* Splits the next valid line into field_count fields; returns 0 at end of file. */
static int next_record(FILE *file, const char *filename, int *line_no,
                       char line[LINE_SIZE], char *fields[], int field_count)
{
    while (fgets(line, LINE_SIZE, file) != NULL) {
        (*line_no)++;

        if (strchr(line, '\n') == NULL && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF);
            printf("WARNING: %s line %d is too long - skipped.\n", filename, *line_no);
            continue;
        }

        char *cursor = trim(line);
        if (*cursor == '\0') {
            continue;
        }

        int found = 0;
        while (found < field_count) {
            fields[found++] = cursor;

            char *comma = (found < field_count) ? strchr(cursor, ',') : NULL;
            if (comma == NULL) {
                break;
            }
            *comma = '\0';
            cursor = comma + 1;
        }

        int ok = (found == field_count);
        for (int i = 0; ok && i < field_count; i++) {
            fields[i] = trim(fields[i]);
            /* '|' is the chain.txt separator. */
            if (fields[i][0] == '\0' || strchr(fields[i], '|') != NULL) {
                ok = 0;
            }
        }

        if (!ok) {
            printf("WARNING: %s line %d should have %d non-empty fields without '|' - skipped.\n",
                   filename, *line_no, field_count);
            continue;
        }

        return 1;
    }

    return 0;
}

static int field_fits(const char *filename, int line_no, const char *name,
                      const char *value, size_t size)
{
    if (strlen(value) < size) {
        return 1;
    }
    printf("WARNING: %s line %d: %s is longer than %zu characters - skipped.\n",
           filename, line_no, name, size - 1);
    return 0;
}

static FILE *open_registry(const char *filename, const char *what)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("ERROR: Could not open '%s'. %s registry is required to start the system.\n",
               filename, what);
    }
    return file;
}

static int finish_registry(FILE *file, const char *filename, int count, const char *what)
{
    fclose(file);

    if (count == 0) {
        printf("ERROR: '%s' is empty or badly formatted. No %s loaded.\n", filename, what);
    }
    return count;
}

int load_books(const char *filename, Book books[])
{
    FILE *file = open_registry(filename, "Book");
    if (file == NULL) {
        return 0;
    }

    char line[LINE_SIZE];
    char *fields[3];
    int line_no = 0;
    int count = 0;

    while (next_record(file, filename, &line_no, line, fields, 3)) {
        if (!field_fits(filename, line_no, "book_id", fields[0], sizeof(books[0].book_id)) ||
            !field_fits(filename, line_no, "title",   fields[1], sizeof(books[0].title)) ||
            !field_fits(filename, line_no, "author",  fields[2], sizeof(books[0].author))) {
            continue;
        }
        if (find_book(books, count, fields[0]) != -1) {
            printf("WARNING: %s line %d: duplicate book ID %s - skipped.\n", filename, line_no, fields[0]);
            continue;
        }
        if (count == MAX_BOOKS) {
            printf("WARNING: %s has more than %d books - the rest were ignored.\n", filename, MAX_BOOKS);
            break;
        }

        strcpy(books[count].book_id, fields[0]);
        strcpy(books[count].title, fields[1]);
        strcpy(books[count].author, fields[2]);
        count++;
    }

    return finish_registry(file, filename, count, "books");
}

int load_members(const char *filename, Member members[])
{
    FILE *file = open_registry(filename, "Member");
    if (file == NULL) {
        return 0;
    }

    char line[LINE_SIZE];
    char *fields[3];
    int line_no = 0;
    int count = 0;

    while (next_record(file, filename, &line_no, line, fields, 3)) {
        if (!field_fits(filename, line_no, "member_id",   fields[0], sizeof(members[0].member_id)) ||
            !field_fits(filename, line_no, "full_name",   fields[1], sizeof(members[0].full_name)) ||
            !field_fits(filename, line_no, "course_code", fields[2], sizeof(members[0].course_code))) {
            continue;
        }
        if (find_member(members, count, fields[0]) != -1) {
            printf("WARNING: %s line %d: duplicate member ID %s - skipped.\n", filename, line_no, fields[0]);
            continue;
        }
        if (count == MAX_MEMBERS) {
            printf("WARNING: %s has more than %d members - the rest were ignored.\n", filename, MAX_MEMBERS);
            break;
        }

        strcpy(members[count].member_id, fields[0]);
        strcpy(members[count].full_name, fields[1]);
        strcpy(members[count].course_code, fields[2]);
        count++;
    }

    return finish_registry(file, filename, count, "members");
}

/* librarians.txt: librarian_id,full_name,role,pin_hash */
int load_librarians(const char *filename, Librarian librarians[])
{
    FILE *file = open_registry(filename, "Librarian");
    if (file == NULL) {
        return 0;
    }

    char line[LINE_SIZE];
    char *fields[4];
    int line_no = 0;
    int count = 0;

    while (next_record(file, filename, &line_no, line, fields, 4)) {
        if (!field_fits(filename, line_no, "librarian_id", fields[0], sizeof(librarians[0].librarian_id)) ||
            !field_fits(filename, line_no, "full_name",    fields[1], sizeof(librarians[0].full_name))) {
            continue;
        }
        if (strcmp(fields[2], "ADMIN") != 0 && strcmp(fields[2], "LIBRARIAN") != 0) {
            printf("WARNING: %s line %d: role must be ADMIN or LIBRARIAN - skipped.\n", filename, line_no);
            continue;
        }
        if (strlen(fields[3]) != 64) {
            printf("WARNING: %s line %d: pin_hash must be 64 hex characters - skipped.\n", filename, line_no);
            continue;
        }
        if (find_librarian(librarians, count, fields[0]) != -1) {
            printf("WARNING: %s line %d: duplicate librarian ID %s - skipped.\n", filename, line_no, fields[0]);
            continue;
        }
        if (count == MAX_LIBRARIANS) {
            printf("WARNING: %s has more than %d librarians - the rest were ignored.\n", filename, MAX_LIBRARIANS);
            break;
        }

        strcpy(librarians[count].librarian_id, fields[0]);
        strcpy(librarians[count].full_name, fields[1]);
        strcpy(librarians[count].role, fields[2]);
        strcpy(librarians[count].pin_hash, fields[3]);
        count++;
    }

    return finish_registry(file, filename, count, "librarians");
}

int find_book(Book books[], int count, const char *book_id)
{
    for (int i = 0; i < count; i++) {
        /* Exact match, so "BK001EXTRA" is not "BK001". */
        if (strcmp(books[i].book_id, book_id) == 0) {
            return i;
        }
    }

    return -1;
}

int find_member(Member members[], int count, const char *member_id)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(members[i].member_id, member_id) == 0) {
            return i;
        }
    }

    return -1;
}

int find_librarian(Librarian librarians[], int count, const char *librarian_id)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(librarians[i].librarian_id, librarian_id) == 0) {
            return i;
        }
    }

    return -1;
}
