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

