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

// ======================= FILING =======================

string dataPath() {
    char b[MAX_PATH];
    GetModuleFileNameA(0, b, MAX_PATH);

    string s = b;
    size_t k = s.find_last_of("\\/");

    return (k == string::npos ? string("") : s.substr(0, k + 1)) + "wow_data.txt";
}

// Format:
//   WOW1
//   P|id|name|opening|balance|dd|mm|yyyy
//   T|change|balanceAfter|serial      (belongs to the P line above it)
void saveData() {
    string path = dataPath(), tmp = path + ".tmp";

    FILE *f = fopen(tmp.c_str(), "w");

    if (!f)
        return;

    fprintf(f, "WOW1\n");

    for (Person *p = listHead; p; p = p->next) {
        fprintf(f, "P|%d|%s|%.2f|%.2f|%d|%d|%d\n",p->id, p->name.c_str(), p->opening, p->bal,p->due.d, p->due.m, p->due.y);

        for (Txn *t = p->head; t; t = t->next)
            fprintf(f, "T|%.2f|%.2f|%d\n", t->amt, t->balAfter, t->serial);
    }

    fclose(f);

    // write to a temp file first so a crash can't corrupt the real one
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

void loadData() {
    FILE *f = fopen(dataPath().c_str(), "r");

    if (!f)
        return;

    char line[512];

    if (!fgets(line, sizeof line, f) || strncmp(line, "WOW1", 4) != 0) {
        fclose(f);
        return;
    }

    Person *cur = 0;

    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'P') {
            int id, d, m, y;
            char name[128];
            double op, bal;

            cur = 0;

            if (sscanf(line, "P|%d|%127[^|]|%lf|%lf|%d|%d|%d",
                       &id, name, &op, &bal, &d, &m, &y) != 7)
                continue;

            if (id < 1 || findId(id) || !validDate(d, m, y))
                continue;

            Date due = {d, m, y};

            cur = newPerson(id, name, op, bal, due);
            linkPerson(cur);
        }
        else if (line[0] == 'T' && cur) {
            double amt, after;
            int serial;

            if (sscanf(line, "T|%lf|%lf|%d", &amt, &after, &serial) != 3)
                continue;

            appendTxn(cur, newTxn(amt, after, serial));
            cur->cnt = serial;
        }
    }

    fclose(f);
}

// ======================= GUI STATE =======================

enum {
    ID_ADD = 100, ID_VIEW, ID_SID, ID_SNAME, ID_GAVE, ID_GOT, ID_HIST, ID_ENAME, ID_EDUE,
    ID_DEL, ID_UNDO, ID_REDO, ID_DASH, ID_SBAL, ID_SNAM, ID_SID2, ID_DUE, ID_EXIT,
    ID_OK, ID_CANCEL
};

// input boxes
enum { E_ID, E_NAME, E_AMT, E_DUE };

HWND hMain, eOut;
HWND ed[4];                        // input boxes, indexed by E_*
string out;

// ---- colour palette ----
#define C_DARK  RGB(0x3F, 0x3F, 0x3F)
#define C_GREEN RGB(0x43, 0x7A, 0x5B)
#define C_LIME  RGB(0xB4, 0xCD, 0x93)
#define C_CREAM RGB(0xFB, 0xF5, 0xB7)

// amount colours used in the output pane and key
#define C_OWED_TO_ME RGB(0x00, 0x80, 0x00)   // green: they owe you
#define C_I_OWE      RGB(0xC8, 0x00, 0x00)   // red: you owe them

HBRUSH brDark, brLime, brCream;

const int NBTN = 17;

HWND hTitle, hSub, hHint, hExit;   // header + hint + exit button
HWND hOk, hCancel;                 // Submit / Cancel buttons
HWND hLbl[4];                      // labels above the input boxes
HWND hSec[4];                      // section headings
HWND hBtn[NBTN];                   // action buttons
HWND hKey[3];                      // key: heading, green line, red line
HFONT fBig, fSub, fMid, fBtn, fMono;
RECT keyRc = {0, 0, 0, 0};         // card behind the key panel
HWND hHot = 0;                     // button currently under the mouse
int pending = 0;                   // command waiting for its inputs (0 = none)
const int HDR = 84;                // height of the header area
const int KEYW = 220;              // width of the key panel

// every action button belongs to one of four sections (columns)
struct BtnDef {
    const char *t;
    int id;
    int sec;
};

const char *secNames[4] = {"PEOPLE", "TRANSACTIONS", "OVERVIEW", "SORTING"};

BtnDef btns[NBTN] = {
    // 0 - People
    {"Add Person", ID_ADD, 0},
    {"View All", ID_VIEW, 0},
    {"Search by ID", ID_SID, 0},
    {"Search by Name", ID_SNAME, 0},
    {"Edit Name", ID_ENAME, 0},
    {"Edit Due Date", ID_EDUE, 0},
    {"Delete", ID_DEL, 0},
    // 1 - Transactions
    {"I Gave Money", ID_GAVE, 1},
    {"They Gave Me", ID_GOT, 1},
    {"History", ID_HIST, 1},
    {"Undo", ID_UNDO, 1},
    {"Redo", ID_REDO, 1},
    // 2 - Overview
    {"Dashboard", ID_DASH, 2},
    {"What's Due", ID_DUE, 2},
    // 3 - Sorting
    {"Sort: Balance", ID_SBAL, 3},
    {"Sort: Name", ID_SNAM, 3},
    {"Sort: ID", ID_SID2, 3}
};

// ---- output pane ----

void say(const string &s = "") {
    out += s + "\r\n";
}

// append text to the output pane in a given colour
void appendText(const string &s, COLORREF c) {
    if (s.empty())
        return;

    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);
    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = c;

    SendMessageA(eOut, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageA(eOut, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageA(eOut, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
}

void flush() {
    SetWindowTextA(eOut, "");

    string seg;
    COLORREF cur = C_DARK;

    for (size_t i = 0; i < out.size(); i++) {
        char ch = out[i];

        if (ch == '\x01' || ch == '\x02' || ch == '\x03') {
            appendText(seg, cur);
            seg.clear();
            cur = ch == '\x01' ? C_OWED_TO_ME : ch == '\x02' ? C_I_OWE : C_DARK;
        }
        else
            seg += ch;
    }

    appendText(seg, cur);
    out.clear();

    // show the top of the output
    SendMessageA(eOut, EM_SETSEL, 0, 0);
    SendMessageA(eOut, EM_SCROLLCARET, 0, 0);
}

// ---- reading and checking the input boxes ----

string getText(HWND h) {
    char b[256];
    GetWindowTextA(h, b, sizeof b);
    return trim(b);
}

bool getId(int &id) {
    string s = getText(ed[E_ID]);

    char *e;
    long v = strtol(s.c_str(), &e, 10);

    if (s.empty() || *e || v < 1 || v > 999999) {
        say("Enter a valid ID (whole number 1 - 999999).");
        return false;
    }

    id = (int)v;
    return true;
}

Person *getPerson() {
    int id;

    if (!getId(id))
        return 0;

    Person *p = findId(id);

    if (!p)
        say("No person found with ID " + str(id) + ".");

    return p;
}

bool getName(string &n) {
    n = getText(ed[E_NAME]);
    bool letter = false;

    for (size_t i = 0; i < n.size(); i++) {
        unsigned char c = n[i];

        if (isalpha(c))
            letter = true;
        else if (c != ' ') {
            say("Name can contain letters only (no numbers or symbols).");
            return false;
        }
    }

    if (!letter) {
        say("Name cannot be empty.");
        return false;
    }

    return true;
}

bool getAmt(double &v, bool allowNeg, bool allowZero) {
    string s = getText(ed[E_AMT]);

    char *e;
    v = strtod(s.c_str(), &e);

    if (s.empty() || *e || fabs(v) > LIM) {
        say("Invalid amount. Enter a number (max 99999999).");
        return false;
    }

    if (v < 0 && !allowNeg) {
        say("Amount cannot be negative.");
        return false;
    }

    if (v == 0 && !allowZero) {
        say("Amount cannot be zero.");
        return false;
    }

    return true;
}

bool getDue(Date &d) {
    if (!parseDate(getText(ed[E_DUE]), d)) {
        say("Invalid due date. Use DD-MM-YYYY.");
        return false;
    }

    if (dayNum(d) < dayNum(today())) {
        say("Due date cannot be in the past. Today is " + dateStr(today()) + ".");
        return false;
    }

    return true;
}

// ======================= DISPLAY =======================

void showTable(Person **a, int n) {
    if (n == 0) {
        say("Nothing to show.");
        return;
    }

    say(fmt("%-6s %-20s %13s  %-13s %-10s", "ID", "Name", "Balance", "Status", "Due"));
    say(rep(68, '-'));

    for (int i = 0; i < n; i++) {
        Person *p = a[i];

        say(fmt("%-6d %-20.20s ", p->id, p->name.c_str()) + tag(p->bal, fmt("%13.2f", p->bal)) + fmt("  %-13s %-10s", status(p->bal).c_str(), dateStr(p->due).c_str()));
    }

    say(rep(68, '-'));
    say(str(n) + " record(s)");
}

void showCard(Person *p) {
    say("ID           : " + str(p->id));
    say("Name         : " + p->name);
    say("Balance      : " + tag(p->bal, money(p->bal)));
    say("Due date     : " + dateStr(p->due));
    say("Transactions : " + str(p->cnt));
}

// ======================= FEATURES =======================

void doAdd() {
    string name;
    double bal;
    Date due;

    if (!getName(name) || !getAmt(bal, true, true) || !getDue(due))
        return;

    // new ID = highest existing ID + 1
    int id = 1;

    for (Person *q = listHead; q; q = q->next)
        if (q->id >= id)
            id = q->id + 1;

    if (id > 999999) {
        say("ID limit reached (999999).");
        return;
    }

    Person *p = newPerson(id, name, bal, bal, due);
    linkPerson(p);

    say("Person added successfully.");
    say();
    showCard(p);
}

void doView() {
    int n;
    Person **a = gatherAll(n);

    showTable(a, n);

    delete[] a;
}

void doSearchId() {
    Person *p = getPerson();

    if (p)
        showCard(p);
}

void doSearchName() {
    string n = getText(ed[E_NAME]);

    if (n.empty()) {
        say("Type a name in the Name box.");
        return;
    }

    string ln = lower(n);
    Person **a = new Person *[personCount > 0 ? personCount : 1];
    int c = 0;

    for (HNode *h = table[hashName(n)]; h; h = h->next)
        if (lower(h->p->name) == ln)
            a[c++] = h->p;

    if (!c)
        say("No person found with that name.");
    else {
        sortArr(a, c, byId);
        showTable(a, c);
    }

    delete[] a;
}

void doTxn(bool gave) {
    Person *p = getPerson();
    double amt;

    if (!p || !getAmt(amt, false, false))
        return;

    double delta = gave ? -amt : amt;
    double before = p->bal;

    if (fabs(before + delta) > LIM) {
        say("Resulting balance is too large. Transaction rejected.");
        return;
    }

    addTxn(p, delta);
    push(undoTop, p->id, delta, before);
    clearStack(redoTop);

    say("Transaction recorded. New balance: " + tag(p->bal, money(p->bal)));

    if ((theyOwe(before) && youOwe(p->bal)) || (youOwe(before) && theyOwe(p->bal)))
        say("Balance crossed zero - sides have switched!");
}

void doHistory() {
    Person *p = getPerson();

    if (!p)
        return;

    say(p->name + " (ID " + str(p->id) + ")");
    say(fmt("%-6s %-16s %16s", "No.", "Change", "Balance After"));
    say(rep(40, '-'));

    say(fmt("%-6s %-16s ", "0", "Opening") + tag(p->opening, fmt("%16.2f", p->opening)));

    for (Txn *t = p->head; t; t = t->next)
        say(fmt("%-6d ", t->serial) + tag(t->amt, fmt("%+-16.2f", t->amt)) + " " + tag(t->balAfter, fmt("%16.2f", t->balAfter)));

    say(rep(40, '-'));
    say("Current balance: " + tag(p->bal, money(p->bal)));
}

void doEditName() {
    Person *p = getPerson();
    string n;

    if (!p || !getName(n))
        return;

    hashRemove(p);
    p->name = n;
    hashInsert(p);

    say("Name updated.");
    showCard(p);
}

void doEditDue() {
    Person *p = getPerson();
    Date d;

    if (!p || !getDue(d))
        return;

    p->due = d;

    say("Due date updated.");
    showCard(p);
}

void doDelete() {
    Person *p = getPerson();

    if (!p)
        return;

    string msg = "Delete " + p->name + " and all history?";

    if (MessageBoxA(hMain, msg.c_str(), "Confirm delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        deletePerson(p);
        say("Person deleted.");
    }
    else
        say("Cancelled.");
}

void doUndo() {
    Op o;

    if (!pop(undoTop, o)) {
        say("Nothing to undo.");
        return;
    }

    Person *p = findId(o.id);

    if (!p) {
        say("That person no longer exists.");
        return;
    }

    removeLastTxn(p);
    p->bal = o.before;

    push(redoTop, o.id, o.amt, o.before);

    say("Undone: " + p->name + " balance restored to " + tag(p->bal, money(p->bal)));
}

void doRedo() {
    Op o;

    if (!pop(redoTop, o)) {
        say("Nothing to redo.");
        return;
    }

    Person *p = findId(o.id);

    if (!p) {
        say("That person no longer exists.");
        return;
    }

    addTxn(p, o.amt);
    p->bal = o.before + o.amt;
    p->tail->balAfter = p->bal;

    push(undoTop, o.id, o.amt, o.before);

    say("Redone: " + p->name + " balance is now " + tag(p->bal, money(p->bal)));
}

