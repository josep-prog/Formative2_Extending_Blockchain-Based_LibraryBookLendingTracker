#ifndef REGISTRY_H
#define REGISTRY_H


#define MAX_BOOKS      100
#define MAX_MEMBERS    100
#define MAX_LIBRARIANS 20

typedef struct {
    char book_id[20];
    char title[80];
    char author[50];
} Book;


typedef struct {
    char member_id[20];
    char full_name[50];
    char course_code[10];
} Member;


typedef struct {
    char librarian_id[20];
    char full_name[50];
    char role[12];         /* "ADMIN" or "LIBRARIAN" */
    char pin_hash[65];
} Librarian;


int load_books(const char *filename, Book books[]);
int load_members(const char *filename, Member members[]);
int load_librarians(const char *filename, Librarian librarians[]);

int find_book(Book books[], int count, const char *book_id);
int find_member(Member members[], int count, const char *member_id);
int find_librarian(Librarian librarians[], int count, const char *librarian_id);

#endif
