// WOW - Who Owes Who (I OWE YOU) -- Win32 GUI version (with data filing)

#include <windows.h>
#include <richedit.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cctype>
using namespace std;

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

const double LIM = 99999999.0;
const double EPS = 0.004;   // balances smaller than this count as settled

// ======================= HELPERS =======================

// printf that returns a string
string fmt(const char *f, ...) {
    char b[512];
    va_list a;

    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);

    return b;
}

string rep(int n, char c) { return string(n > 0 ? n : 0, c); }
string str(long v)        { return fmt("%ld", v); }
string money(double v)    { return fmt("%.2f", v); }

string lower(string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = tolower((unsigned char)s[i]);

    return s;
}

string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");

    if (a == string::npos)
        return "";

    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Negative balance = they owe you, positive = you owe them.
bool theyOwe(double b) { return b < -EPS; }
bool youOwe(double b)  { return b > EPS; }

string status(double b) {
    return theyOwe(b) ? "They owe you" : youOwe(b) ? "You owe" : "Settled";
}

// Colour markers embedded in output text (handled in flush):
//   \x01 = start green (they owe you), \x02 = start red (you owe), \x03 = end colour
string tag(double v, const string &s) {
    if (theyOwe(v))
        return "\x01" + s + "\x03";

    if (youOwe(v))
        return "\x02" + s + "\x03";

    return s;
}

// ======================= DATE =======================

struct Date {
    int d, m, y;
};

int dim(int m, int y) {
    static int t[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;

    return t[m - 1];
}

bool validDate(int d, int m, int y) {
    return y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= dim(m, y);
}

bool parseDate(string s, Date &o) {
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == '/' || s[i] == '.')
            s[i] = '-';

    int d, m, y;
    char extra;

    if (sscanf(s.c_str(), "%d-%d-%d%c", &d, &m, &y, &extra) != 3 || !validDate(d, m, y))
        return false;

    o.d = d;
    o.m = m;
    o.y = y;

    return true;
}

long dayNum(const Date &t) {
    long y = t.y, m = t.m, d = t.d;

    y -= m <= 2;

    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe - 719468;
}

Date today() {
    time_t t = time(0);
    tm *l = localtime(&t);

    Date d = {l->tm_mday, l->tm_mon + 1, l->tm_year + 1900};

    return d;
}

string dateStr(const Date &d) {
    return fmt("%02d-%02d-%04d", d.d, d.m, d.y);
}

// ======================= DATA STRUCTURES =======================

struct Txn {
    double amt, balAfter;
    int serial;
    Txn *next;
};

struct Person {
    int id;
    string name;
    double opening, bal;
    Date due;
    Txn *head, *tail;
    int cnt;
    Person *next;
};

Person *listHead = 0, *listTail = 0;
int personCount = 0;

// ---- BST (by ID) ----

struct Node {
    int id;
    Person *p;
    Node *l, *r;
};

Node *root = 0;

void bstInsert(Node *&r, Person *p) {
    if (!r) {
        r = new Node;
        r->id = p->id;
        r->p = p;
        r->l = r->r = 0;
        return;
    }

    bstInsert(p->id < r->id ? r->l : r->r, p);
}

Person *bstSearch(int id) {
    Node *c = root;

    while (c) {
        if (id == c->id)
            return c->p;

        c = id < c->id ? c->l : c->r;
    }

    return 0;
}

Node *bstDelete(Node *r, int id) {
    if (!r)
        return 0;

    if (id < r->id)
        r->l = bstDelete(r->l, id);
    else if (id > r->id)
        r->r = bstDelete(r->r, id);
    else {
        if (!r->l) {
            Node *t = r->r;
            delete r;
            return t;
        }

        if (!r->r) {
            Node *t = r->l;
            delete r;
            return t;
        }

        Node *s = r->r;

        while (s->l)
            s = s->l;

        r->id = s->id;
        r->p = s->p;
        r->r = bstDelete(r->r, r->id);
    }

    return r;
}

void inorder(Node *r, Person **a, int &i) {
    if (!r)
        return;

    inorder(r->l, a, i);
    a[i++] = r->p;
    inorder(r->r, a, i);
}

// ---- hash table (by name) ----

const int HS = 101;

struct HNode {
    Person *p;
    HNode *next;
};

HNode *table[HS];

int hashName(const string &n) {
    unsigned long h = 5381;
    string s = lower(n);

    for (size_t i = 0; i < s.size(); i++)
        h = h * 33 + (unsigned char)s[i];

    return (int)(h % HS);
}

void hashInsert(Person *p) {
    int k = hashName(p->name);

    HNode *n = new HNode;
    n->p = p;
    n->next = table[k];
    table[k] = n;
}

void hashRemove(Person *p) {
    HNode **pp = &table[hashName(p->name)];

    while (*pp) {
        if ((*pp)->p == p) {
            HNode *d = *pp;
            *pp = d->next;
            delete d;
            return;
        }

        pp = &(*pp)->next;
    }
}

// ---- undo / redo stacks ----

struct Op {
    int id;
    double amt, before;
    Op *next;
};

Op *undoTop = 0, *redoTop = 0;

void push(Op *&top, int id, double amt, double before) {
    Op *o = new Op;
    o->id = id;
    o->amt = amt;
    o->before = before;
    o->next = top;
    top = o;
}

bool pop(Op *&top, Op &out) {
    if (!top)
        return false;

    Op *t = top;
    out = *t;
    top = t->next;
    delete t;

    return true;
}

void clearStack(Op *&top) {
    while (top) {
        Op *t = top;
        top = t->next;
        delete t;
    }
}

// remove every entry that belongs to one person
void purge(Op *&top, int id) {
    Op **pp = &top;

    while (*pp) {
        if ((*pp)->id == id) {
            Op *d = *pp;
            *pp = d->next;
            delete d;
        }
        else
            pp = &(*pp)->next;
    }
}

// ======================= MERGE SORT =======================

typedef bool (*Less)(Person *, Person *);

void msort(Person **a, Person **t, int lo, int hi, Less lt) {
    if (hi - lo < 1)
        return;

    int mid = (lo + hi) / 2;

    msort(a, t, lo, mid, lt);
    msort(a, t, mid + 1, hi, lt);

    int i = lo, j = mid + 1, k = lo;

    while (i <= mid && j <= hi)
        t[k++] = lt(a[j], a[i]) ? a[j++] : a[i++];

    while (i <= mid)
        t[k++] = a[i++];

    while (j <= hi)
        t[k++] = a[j++];

    for (k = lo; k <= hi; k++)
        a[k] = t[k];
}

void sortArr(Person **a, int n, Less lt) {
    if (n < 2)
        return;

    Person **t = new Person *[n];

    msort(a, t, 0, n - 1, lt);

    delete[] t;
}

bool byId(Person *a, Person *b)   { return a->id < b->id; }
bool byBal(Person *a, Person *b)  { return a->bal < b->bal; }
bool byName(Person *a, Person *b) { return lower(a->name) < lower(b->name); }

bool byDue(Person *a, Person *b) {
    long x = dayNum(a->due), y = dayNum(b->due);

    if (x != y)
        return x < y;

    return a->bal < b->bal;
}

// ======================= CORE OPERATIONS =======================

Person *findId(int id) {
    return bstSearch(id);
}

Person **gatherAll(int &n) {
    n = personCount;

    Person **a = new Person *[n > 0 ? n : 1];
    int i = 0;

    for (Person *p = listHead; p; p = p->next)
        a[i++] = p;

    return a;
}

Txn *newTxn(double amt, double balAfter, int serial) {
    Txn *t = new Txn;

    t->amt = amt;
    t->balAfter = balAfter;
    t->serial = serial;
    t->next = 0;

    return t;
}

void appendTxn(Person *p, Txn *t) {
    if (!p->head)
        p->head = p->tail = t;
    else {
        p->tail->next = t;
        p->tail = t;
    }
}

void addTxn(Person *p, double delta) {
    p->bal += delta;
    appendTxn(p, newTxn(delta, p->bal, ++p->cnt));
}

void removeLastTxn(Person *p) {
    if (!p->head)
        return;

    if (p->head == p->tail) {
        delete p->head;
        p->head = p->tail = 0;
    }
    else {
        Txn *c = p->head;

        while (c->next != p->tail)
            c = c->next;

        delete p->tail;
        p->tail = c;
        c->next = 0;
    }

    p->cnt--;
}

Person *newPerson(int id, const string &name, double opening, double bal, const Date &due) {
    Person *p = new Person;

    p->id = id;
    p->name = name;
    p->opening = opening;
    p->bal = bal;
    p->due = due;
    p->head = p->tail = 0;
    p->cnt = 0;

    return p;
}

// append a new person to the list, BST and hash table
void linkPerson(Person *p) {
    p->next = 0;

    if (!listHead)
        listHead = listTail = p;
    else {
        listTail->next = p;
        listTail = p;
    }

    bstInsert(root, p);
    hashInsert(p);
    personCount++;
}

void deletePerson(Person *p) {
    Person *prev = 0, *c = listHead;

    while (c && c != p) {
        prev = c;
        c = c->next;
    }

    if (!c)
        return;

    if (prev)
        prev->next = c->next;
    else
        listHead = c->next;

    if (listTail == c)
        listTail = prev;

    hashRemove(c);
    root = bstDelete(root, c->id);

    purge(undoTop, c->id);
    purge(redoTop, c->id);

    while (c->head) {
        Txn *t = c->head;
        c->head = t->next;
        delete t;
    }

    delete c;
    personCount--;
}

