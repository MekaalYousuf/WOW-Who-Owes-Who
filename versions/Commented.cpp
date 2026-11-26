// WOW - Who Owes Who (I OWE YOU)
// Win32 GUI version with Data Filing

#include <windows.h>       // Win32 GUI functions
#include <richedit.h>      // Rich Edit control for coloured output
#include <string>
#include <cstdio>           // File handling and formatted input/output
#include <cstdarg>          // Variable argument functions
#include <cstdlib>          // strtol, strtod
#include <cstring>          // String functions
#include <cmath>             // fabs
#include <ctime>             // Current date
#include <cctype>            // Character checking
using namespace std;

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

const double LIM = 99999999.0;
const double EPS = 0.004;    // Small balances count as settled


// ============================================================
// 1. HELPER FUNCTIONS
// ============================================================

// printf-style formatting that returns a C++ string
string fmt(const char *f, ...) {
    char b[512];
    va_list a;

    va_start(a, f);                    // Start reading arguments
    vsnprintf(b, sizeof b, f, a);      // Format into character array
    va_end(a);

    return b;
}

string rep(int n, char c) {
    return string(n > 0 ? n : 0, c);  // Repeat a character n times
}

string str(long v) {
    return fmt("%ld", v);             // Convert number to string
}

string money(double v) {
    return fmt("%.2f", v);            // Money with 2 decimal places
}

// Convert a string to lowercase
string lower(string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = tolower((unsigned char)s[i]);

    return s;
}

// Remove spaces from beginning and end
string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");

    if (a == string::npos)
        return "";

    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Negative balance = person owes us
// Positive balance = we owe the person
bool theyOwe(double b) {
    return b < -EPS;
}

bool youOwe(double b) {
    return b > EPS;
}

// Return balance status
string status(double b) {
    return theyOwe(b) ? "They owe you" :
           youOwe(b) ? "You owe" : "Settled";
}

// Insert colour markers into output
// \x01 = green, \x02 = red, \x03 = normal
string tag(double v, const string &s) {
    if (theyOwe(v))
        return "\x01" + s + "\x03";

    if (youOwe(v))
        return "\x02" + s + "\x03";

    return s;
}


// ============================================================
// 2. DATE HANDLING
// ============================================================

struct Date {
    int d, m, y;                       // Day, month, year
};

// Return number of days in a month
int dim(int m, int y) {
    static int t[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    // Check leap year
    if (m == 2 &&
        ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;

    return t[m - 1];
}

// Validate date
bool validDate(int d, int m, int y) {
    return y >= 2000 && y <= 2100 &&
           m >= 1 && m <= 12 &&
           d >= 1 && d <= dim(m, y);
}

// Convert DD/MM/YYYY or DD.MM.YYYY into DD-MM-YYYY
bool parseDate(string s, Date &o) {
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == '/' || s[i] == '.')
            s[i] = '-';

    int d, m, y;
    char extra;

    if (sscanf(s.c_str(), "%d-%d-%d%c",
               &d, &m, &y, &extra) != 3 ||
        !validDate(d, m, y))
        return false;

    o.d = d;
    o.m = m;
    o.y = y;

    return true;
}

// Convert date into number of days
// Useful for comparing two dates
long dayNum(const Date &t) {
    long y = t.y, m = t.m, d = t.d;

    y -= m <= 2;

    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe - 719468;
}

// Get today's date from system clock
Date today() {
    time_t t = time(0);
    tm *l = localtime(&t);

    Date d = {
        l->tm_mday,
        l->tm_mon + 1,
        l->tm_year + 1900
    };

    return d;
}

// Convert date into DD-MM-YYYY format
string dateStr(const Date &d) {
    return fmt("%02d-%02d-%04d", d.d, d.m, d.y);
}


// ============================================================
// 3. DATA STRUCTURES
// ============================================================

// Linked list node for transaction history
struct Txn {
    double amt;                        // Transaction amount
    double balAfter;                   // Balance after transaction
    int serial;                        // Transaction number
    Txn *next;                          // Next transaction
};

// Linked list node for a person
struct Person {
    int id;
    string name;
    double opening;                    // Initial balance
    double bal;                        // Current balance
    Date due;

    Txn *head, *tail;                  // Transaction linked list
    int cnt;

    Person *next;                      // Next person
};

// Main linked list
Person *listHead = 0, *listTail = 0;
int personCount = 0;


// ============================================================
// 4. BINARY SEARCH TREE (BST) - SEARCH BY ID
// ============================================================

struct Node {
    int id;
    Person *p;                         // Points to actual person
    Node *l, *r;                       // Left and right children
};

Node *root = 0;

// Insert person into BST using ID
void bstInsert(Node *&r, Person *p) {
    if (!r) {
        r = new Node;

        r->id = p->id;
        r->p = p;
        r->l = r->r = 0;

        return;
    }

    // Smaller ID goes left, larger ID goes right
    bstInsert(p->id < r->id ? r->l : r->r, p);
}

// Search BST by ID
Person *bstSearch(int id) {
    Node *c = root;

    while (c) {
        if (id == c->id)
            return c->p;

        c = id < c->id ? c->l : c->r;
    }

    return 0;                          // Not found
}

// Delete a BST node by ID
Node *bstDelete(Node *r, int id) {
    if (!r)
        return 0;

    if (id < r->id)
        r->l = bstDelete(r->l, id);

    else if (id > r->id)
        r->r = bstDelete(r->r, id);

    else {
        // Case 1: No left child
        if (!r->l) {
            Node *t = r->r;
            delete r;
            return t;
        }

        // Case 2: No right child
        if (!r->r) {
            Node *t = r->l;
            delete r;
            return t;
        }

        // Case 3: Two children
        // Find inorder successor (smallest in right subtree)
        Node *s = r->r;

        while (s->l)
            s = s->l;

        r->id = s->id;
        r->p = s->p;

        r->r = bstDelete(r->r, r->id);
    }

    return r;
}

// Inorder traversal gives IDs in sorted order
void inorder(Node *r, Person **a, int &i) {
    if (!r)
        return;

    inorder(r->l, a, i);               // Left
    a[i++] = r->p;                     // Root
    inorder(r->r, a, i);               // Right
}


// ============================================================
// 5. HASH TABLE - SEARCH BY NAME
// ============================================================

const int HS = 101;                    // Hash table size

struct HNode {
    Person *p;
    HNode *next;                       // Collision linked list
};

HNode *table[HS];

// Hash function using name
int hashName(const string &n) {
    unsigned long h = 5381;
    string s = lower(n);

    for (size_t i = 0; i < s.size(); i++)
        h = h * 33 + (unsigned char)s[i];

    return (int)(h % HS);              // Bucket index
}

// Insert person into hash table
// Separate chaining handles collisions
void hashInsert(Person *p) {
    int k = hashName(p->name);

    HNode *n = new HNode;
    n->p = p;

    n->next = table[k];                // Insert at beginning
    table[k] = n;
}

// Remove person from hash table
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


// ============================================================
// 6. UNDO / REDO STACKS
// ============================================================

// Stack node stores transaction information
struct Op {
    int id;
    double amt;
    double before;                     // Balance before transaction
    Op *next;
};

Op *undoTop = 0, *redoTop = 0;

// Push operation onto stack
void push(Op *&top, int id, double amt, double before) {
    Op *o = new Op;

    o->id = id;
    o->amt = amt;
    o->before = before;

    o->next = top;                     // Link to previous top
    top = o;                           // Update top
}

// Pop operation from stack
bool pop(Op *&top, Op &out) {
    if (!top)
        return false;

    Op *t = top;
    out = *t;

    top = t->next;
    delete t;

    return true;
}

// Delete all nodes in a stack
void clearStack(Op *&top) {
    while (top) {
        Op *t = top;
        top = t->next;
        delete t;
    }
}

// Remove all operations belonging to a deleted person
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


// ============================================================
// 7. MERGE SORT
// ============================================================

// Function pointer for selecting sorting condition
typedef bool (*Less)(Person *, Person *);

// Recursive Merge Sort
void msort(Person **a, Person **t,
           int lo, int hi, Less lt) {

    if (hi - lo < 1)
        return;                        // Base case

    int mid = (lo + hi) / 2;

    msort(a, t, lo, mid, lt);          // Sort left half
    msort(a, t, mid + 1, hi, lt);      // Sort right half

    int i = lo, j = mid + 1, k = lo;

    // Merge the two sorted halves
    while (i <= mid && j <= hi)
        t[k++] = lt(a[j], a[i]) ? a[j++] : a[i++];

    while (i <= mid)
        t[k++] = a[i++];

    while (j <= hi)
        t[k++] = a[j++];

    // Copy sorted data back
    for (k = lo; k <= hi; k++)
        a[k] = t[k];
}

// Wrapper function for Merge Sort
void sortArr(Person **a, int n, Less lt) {
    if (n < 2)
        return;

    Person **t = new Person *[n];      // Temporary array

    msort(a, t, 0, n - 1, lt);

    delete[] t;
}

// Different sorting conditions
bool byId(Person *a, Person *b) {
    return a->id < b->id;
}

bool byBal(Person *a, Person *b) {
    return a->bal < b->bal;
}

bool byName(Person *a, Person *b) {
    return lower(a->name) < lower(b->name);
}

bool byDue(Person *a, Person *b) {
    long x = dayNum(a->due);
    long y = dayNum(b->due);

    if (x != y)
        return x < y;

    return a->bal < b->bal;
}


// ============================================================
// 8. CORE OPERATIONS
// ============================================================

// Search person by ID
Person *findId(int id) {
    return bstSearch(id);
}

// Copy all linked-list person pointers into an array
Person **gatherAll(int &n) {
    n = personCount;

    Person **a = new Person *[n > 0 ? n : 1];
    int i = 0;

    for (Person *p = listHead; p; p = p->next)
        a[i++] = p;

    return a;
}

// Create a new transaction node
Txn *newTxn(double amt, double balAfter, int serial) {
    Txn *t = new Txn;

    t->amt = amt;
    t->balAfter = balAfter;
    t->serial = serial;
    t->next = 0;

    return t;
}

// Append transaction to person's history
void appendTxn(Person *p, Txn *t) {
    if (!p->head)
        p->head = p->tail = t;         // First transaction
    else {
        p->tail->next = t;
        p->tail = t;
    }
}

// Update balance and add transaction
void addTxn(Person *p, double delta) {
    p->bal += delta;

    appendTxn(p, newTxn(delta, p->bal, ++p->cnt));
}

// Remove most recent transaction
void removeLastTxn(Person *p) {
    if (!p->head)
        return;

    // Only one transaction
    if (p->head == p->tail) {
        delete p->head;
        p->head = p->tail = 0;
    }
    else {
        Txn *c = p->head;

        // Find second-last transaction
        while (c->next != p->tail)
            c = c->next;

        delete p->tail;

        p->tail = c;
        c->next = 0;
    }

    p->cnt--;
}

// Create a new person dynamically
Person *newPerson(int id, const string &name,
                  double opening, double bal,
                  const Date &due) {

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

// Insert person into all three structures
void linkPerson(Person *p) {
    p->next = 0;

    // Insert into linked list
    if (!listHead)
        listHead = listTail = p;
    else {
        listTail->next = p;
        listTail = p;
    }

    bstInsert(root, p);                // Insert into BST
    hashInsert(p);                     // Insert into hash table

    personCount++;
}

// Delete person from every data structure
void deletePerson(Person *p) {
    Person *prev = 0, *c = listHead;

    // Find person in linked list
    while (c && c != p) {
        prev = c;
        c = c->next;
    }

    if (!c)
        return;

    // Remove from linked list
    if (prev)
        prev->next = c->next;
    else
        listHead = c->next;

    if (listTail == c)
        listTail = prev;

    hashRemove(c);                     // Remove from hash table
    root = bstDelete(root, c->id);     // Remove from BST

    purge(undoTop, c->id);             // Remove undo operations
    purge(redoTop, c->id);             // Remove redo operations

    // Free transaction history
    while (c->head) {
        Txn *t = c->head;
        c->head = t->next;
        delete t;
    }

    delete c;                          // Free person node
    personCount--;
}


// ============================================================
// 9. FILE HANDLING / DATA FILING
// ============================================================

// Get path of wow_data.txt beside executable
string dataPath() {
    char b[MAX_PATH];

    GetModuleFileNameA(0, b, MAX_PATH);

    string s = b;
    size_t k = s.find_last_of("\\/");

    return (k == string::npos ?
            string("") : s.substr(0, k + 1))
            + "wow_data.txt";
}

// Save people and transaction history
void saveData() {
    string path = dataPath();
    string tmp = path + ".tmp";

    FILE *f = fopen(tmp.c_str(), "w");

    if (!f)
        return;

    fprintf(f, "WOW1\n");              // File version header

    // Save every person
    for (Person *p = listHead; p; p = p->next) {

        fprintf(f, "P|%d|%s|%.2f|%.2f|%d|%d|%d\n",
                p->id, p->name.c_str(),
                p->opening, p->bal,
                p->due.d, p->due.m, p->due.y);

        // Save that person's transactions
        for (Txn *t = p->head; t; t = t->next)
            fprintf(f, "T|%.2f|%.2f|%d\n",
                    t->amt, t->balAfter, t->serial);
    }

    fclose(f);

    // Replace original file with temporary file
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

// Load saved records from file
void loadData() {
    FILE *f = fopen(dataPath().c_str(), "r");

    if (!f)
        return;

    char line[512];

    // Check file version
    if (!fgets(line, sizeof line, f) ||
        strncmp(line, "WOW1", 4) != 0) {

        fclose(f);
        return;
    }

    Person *cur = 0;

    // Read file line by line
    while (fgets(line, sizeof line, f)) {

        // Person record
        if (line[0] == 'P') {
            int id, d, m, y;
            char name[128];
            double op, bal;

            cur = 0;

            // Extract person information
            if (sscanf(line, "P|%d|%127[^|]|%lf|%lf|%d|%d|%d",
                       &id, name, &op, &bal, &d, &m, &y) != 7)
                continue;

            // Validate loaded record
            if (id < 1 || findId(id) || !validDate(d, m, y))
                continue;

            Date due = {d, m, y};

            cur = newPerson(id, name, op, bal, due);
            linkPerson(cur);
        }

        // Transaction record belonging to current person
        else if (line[0] == 'T' && cur) {
            double amt, after;
            int serial;

            if (sscanf(line, "T|%lf|%lf|%d",
                       &amt, &after, &serial) != 3)
                continue;

            appendTxn(cur, newTxn(amt, after, serial));
            cur->cnt = serial;
        }
    }

    fclose(f);
}


// ============================================================
// 10. GUI STATE AND CONTROL IDENTIFIERS
// ============================================================

// Unique IDs for GUI buttons
enum {
    ID_ADD = 100, ID_VIEW, ID_SID, ID_SNAME,
    ID_GAVE, ID_GOT, ID_HIST, ID_ENAME, ID_EDUE,
    ID_DEL, ID_UNDO, ID_REDO, ID_DASH,
    ID_SBAL, ID_SNAM, ID_SID2, ID_DUE, ID_EXIT,
    ID_OK, ID_CANCEL
};

// Input box indexes
enum {
    E_ID, E_NAME, E_AMT, E_DUE
};

// Window handles
HWND hMain, eOut;
HWND ed[4];                            // Four input boxes

string out;                            // Output buffer

// GUI colours
#define C_DARK  RGB(0x3F, 0x3F, 0x3F)
#define C_GREEN RGB(0x43, 0x7A, 0x5B)
#define C_LIME  RGB(0xB4, 0xCD, 0x93)
#define C_CREAM RGB(0xFB, 0xF5, 0xB7)

#define C_OWED_TO_ME RGB(0x00, 0x80, 0x00)
#define C_I_OWE      RGB(0xC8, 0x00, 0x00)

// Brushes used for backgrounds
HBRUSH brDark, brLime, brCream;

const int NBTN = 17;

// GUI control handles
HWND hTitle, hSub, hHint, hExit;
HWND hOk, hCancel;
HWND hLbl[4];
HWND hSec[4];
HWND hBtn[NBTN];
HWND hKey[3];

HFONT fBig, fSub, fMid, fBtn, fMono;

RECT keyRc = {0, 0, 0, 0};

HWND hHot = 0;                         // Button under mouse
int pending = 0;                       // Currently selected command

const int HDR = 84;
const int KEYW = 220;

// Button information
struct BtnDef {
    const char *t;                     // Button text
    int id;                            // Button ID
    int sec;                           // Section/column
};

const char *secNames[4] = {
    "PEOPLE", "TRANSACTIONS", "OVERVIEW", "SORTING"
};

// All 17 buttons and their sections
BtnDef btns[NBTN] = {
    {"Add Person", ID_ADD, 0},
    {"View All", ID_VIEW, 0},
    {"Search by ID", ID_SID, 0},
    {"Search by Name", ID_SNAME, 0},
    {"Edit Name", ID_ENAME, 0},
    {"Edit Due Date", ID_EDUE, 0},
    {"Delete", ID_DEL, 0},

    {"I Gave Money", ID_GAVE, 1},
    {"They Gave Me", ID_GOT, 1},
    {"History", ID_HIST, 1},
    {"Undo", ID_UNDO, 1},
    {"Redo", ID_REDO, 1},

    {"Dashboard", ID_DASH, 2},
    {"What's Due", ID_DUE, 2},

    {"Sort: Balance", ID_SBAL, 3},
    {"Sort: Name", ID_SNAM, 3},
    {"Sort: ID", ID_SID2, 3}
};


// ============================================================
// 11. OUTPUT FUNCTIONS
// ============================================================

// Add text to output buffer
void say(const string &s = "") {
    out += s + "\r\n";
}

// Append text with a specified colour
void appendText(const string &s, COLORREF c) {
    if (s.empty())
        return;

    CHARFORMATA cf;
    memset(&cf, 0, sizeof cf);

    cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = c;

    // Move cursor to end of Rich Edit
    SendMessageA(eOut, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);

    // Set text colour
    SendMessageA(eOut, EM_SETCHARFORMAT,
                 SCF_SELECTION, (LPARAM)&cf);

    // Insert text
    SendMessageA(eOut, EM_REPLACESEL,
                 FALSE, (LPARAM)s.c_str());
}

// Display buffered output with colour markers
void flush() {
    SetWindowTextA(eOut, "");

    string seg;
    COLORREF cur = C_DARK;

    for (size_t i = 0; i < out.size(); i++) {
        char ch = out[i];

        // Change text colour when marker is encountered
        if (ch == '\x01' || ch == '\x02' || ch == '\x03') {
            appendText(seg, cur);
            seg.clear();

            cur = ch == '\x01' ? C_OWED_TO_ME :
                  ch == '\x02' ? C_I_OWE : C_DARK;
        }
        else
            seg += ch;
    }

    appendText(seg, cur);
    out.clear();

    // Scroll output to beginning
    SendMessageA(eOut, EM_SETSEL, 0, 0);
    SendMessageA(eOut, EM_SCROLLCARET, 0, 0);
}


// ============================================================
// 12. INPUT VALIDATION
// ============================================================

// Read text from an input box
string getText(HWND h) {
    char b[256];

    GetWindowTextA(h, b, sizeof b);

    return trim(b);
}

// Read and validate ID
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

// Get person using ID
Person *getPerson() {
    int id;

    if (!getId(id))
        return 0;

    Person *p = findId(id);

    if (!p)
        say("No person found with ID " + str(id) + ".");

    return p;
}

// Validate name
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

// Validate amount
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

// Validate due date
bool getDue(Date &d) {
    if (!parseDate(getText(ed[E_DUE]), d)) {
        say("Invalid due date. Use DD-MM-YYYY.");
        return false;
    }

    // Due date cannot be before today
    if (dayNum(d) < dayNum(today())) {
        say("Due date cannot be in the past. Today is " +
            dateStr(today()) + ".");
        return false;
    }

    return true;
}


// ============================================================
// 13. DISPLAY FUNCTIONS
// ============================================================

// Display all people in table format
void showTable(Person **a, int n) {
    if (n == 0) {
        say("Nothing to show.");
        return;
    }

    // Table headings
    say(fmt("%-6s %-20s %13s  %-13s %-10s",
            "ID", "Name", "Balance", "Status", "Due"));

    say(rep(68, '-'));

    for (int i = 0; i < n; i++) {
        Person *p = a[i];

        say(fmt("%-6d %-20.20s ", p->id, p->name.c_str()) +
            tag(p->bal, fmt("%13.2f", p->bal)) +
            fmt("  %-13s %-10s",
                status(p->bal).c_str(),
                dateStr(p->due).c_str()));
    }

    say(rep(68, '-'));
    say(str(n) + " record(s)");
}

// Display one person's information
void showCard(Person *p) {
    say("ID           : " + str(p->id));
    say("Name         : " + p->name);
    say("Balance      : " + tag(p->bal, money(p->bal)));
    say("Due date     : " + dateStr(p->due));
    say("Transactions : " + str(p->cnt));
}


// ============================================================
// 14. MAIN FEATURES
// ============================================================

// ADD PERSON
void doAdd() {
    string name;
    double bal;
    Date due;

    // Validate all inputs
    if (!getName(name) ||
        !getAmt(bal, true, true) ||
        !getDue(due))
        return;

    // Generate next available ID
    int id = 1;

    for (Person *q = listHead; q; q = q->next)
        if (q->id >= id)
            id = q->id + 1;

    if (id > 999999) {
        say("ID limit reached (999999).");
        return;
    }

    // Create and insert new person
    Person *p = newPerson(id, name, bal, bal, due);

    linkPerson(p);

    say("Person added successfully.");
    say();
    showCard(p);
}

// VIEW ALL PEOPLE
void doView() {
    int n;

    Person **a = gatherAll(n);

    showTable(a, n);

    delete[] a;
}

// SEARCH BY ID
void doSearchId() {
    Person *p = getPerson();

    if (p)
        showCard(p);
}

// SEARCH BY NAME
void doSearchName() {
    string n = getText(ed[E_NAME]);

    if (n.empty()) {
        say("Type a name in the Name box.");
        return;
    }

    string ln = lower(n);

    Person **a = new Person *[personCount > 0 ? personCount : 1];
    int c = 0;

    // Search only the calculated hash bucket
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

// RECORD TRANSACTION
// gave = true means we gave money
// gave = false means they gave us money
void doTxn(bool gave) {
    Person *p = getPerson();
    double amt;

    if (!p || !getAmt(amt, false, false))
        return;

    // Giving money decreases balance
    // Receiving money increases balance
    double delta = gave ? -amt : amt;

    double before = p->bal;

    // Check balance limit
    if (fabs(before + delta) > LIM) {
        say("Resulting balance is too large. Transaction rejected.");
        return;
    }

    addTxn(p, delta);                  // Update balance and history

    push(undoTop, p->id, delta, before); // Store for Undo

    clearStack(redoTop);               // New action clears Redo history

    say("Transaction recorded. New balance: " +
        tag(p->bal, money(p->bal)));

    // Check if direction of debt changed
    if ((theyOwe(before) && youOwe(p->bal)) ||
        (youOwe(before) && theyOwe(p->bal)))
        say("Balance crossed zero - sides have switched!");
}

// TRANSACTION HISTORY
void doHistory() {
    Person *p = getPerson();

    if (!p)
        return;

    say(p->name + " (ID " + str(p->id) + ")");

    say(fmt("%-6s %-16s %16s",
            "No.", "Change", "Balance After"));

    say(rep(40, '-'));

    // Opening balance
    say(fmt("%-6s %-16s ", "0", "Opening") +
        tag(p->opening, fmt("%16.2f", p->opening)));

    // Display each transaction
    for (Txn *t = p->head; t; t = t->next)
        say(fmt("%-6d ", t->serial) +
            tag(t->amt, fmt("%+-16.2f", t->amt)) + " " +
            tag(t->balAfter, fmt("%16.2f", t->balAfter)));

    say(rep(40, '-'));

    say("Current balance: " + tag(p->bal, money(p->bal)));
}

// EDIT PERSON NAME
void doEditName() {
    Person *p = getPerson();
    string n;

    if (!p || !getName(n))
        return;

    hashRemove(p);                    // Remove old hash entry

    p->name = n;                      // Update name

    hashInsert(p);                    // Insert with new hash value

    say("Name updated.");
    showCard(p);
}

// EDIT DUE DATE
void doEditDue() {
    Person *p = getPerson();
    Date d;

    if (!p || !getDue(d))
        return;

    p->due = d;

    say("Due date updated.");
    showCard(p);
}

// DELETE PERSON
void doDelete() {
    Person *p = getPerson();

    if (!p)
        return;

    string msg = "Delete " + p->name + " and all history?";

    // Ask user for confirmation
    if (MessageBoxA(hMain, msg.c_str(), "Confirm delete",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {

        deletePerson(p);

        say("Person deleted.");
    }
    else
        say("Cancelled.");
}

// UNDO LAST TRANSACTION
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

    removeLastTxn(p);                 // Remove latest history entry

    p->bal = o.before;                // Restore previous balance

    push(redoTop, o.id, o.amt, o.before);

    say("Undone: " + p->name +
        " balance restored to " + tag(p->bal, money(p->bal)));
}

// REDO LAST UNDONE TRANSACTION
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

    addTxn(p, o.amt);                 // Apply transaction again

    p->bal = o.before + o.amt;
    p->tail->balAfter = p->bal;

    push(undoTop, o.id, o.amt, o.before);

    say("Redone: " + p->name +
        " balance is now " + tag(p->bal, money(p->bal)));
}

// DASHBOARD
void doDash() {
    double owed = 0, owe = 0;
    int a = 0, b = 0, over = 0;

    long t = dayNum(today());

    // Calculate totals
    for (Person *p = listHead; p; p = p->next) {

        if (theyOwe(p->bal)) {
            owed -= p->bal;
            a++;

            if (dayNum(p->due) < t)
                over++;
        }

        else if (youOwe(p->bal)) {
            owe += p->bal;
            b++;
        }
    }

    double net = owed - owe;

    say("Total owed to you : " +
        tag(-1, money(owed)) + "  (" + str(a) + " people)");

    say("Total you owe     : " +
        tag(1, money(owe)) + "  (" + str(b) + " people)");

    say(rep(46, '-'));

    say("Net position      : " +
        tag(-net, money(fabs(net))) +
        (net > EPS ? "  in your favour" :
         net < -EPS ? "  against you" : "  even"));

    say();

    say("People in khata   : " + str(personCount));
    say("Overdue payments  : " + str(over));
}

// SORT PEOPLE
void doSorted(int c) {
    int n;

    Person **a = gatherAll(n);

    if (c == 1) {
        sortArr(a, n, byBal);
        say("SORTED BY BALANCE (most owed to you first)");
    }

    else if (c == 2) {
        sortArr(a, n, byName);
        say("SORTED BY NAME");
    }

    else {
        int i = 0;

        inorder(root, a, i);           // BST inorder = sorted IDs

        say("SORTED BY ID (BST in-order)");
    }

    say();
    showTable(a, n);

    delete[] a;
}

// SHOW WHAT IS DUE
void doDue() {
    int n = 0;

    Person **a = new Person *[personCount > 0 ? personCount : 1];

    // Only include people who owe us
    for (Person *p = listHead; p; p = p->next)
        if (theyOwe(p->bal))
            a[n++] = p;

    if (!n) {
        say("Nobody owes you anything right now.");
        delete[] a;
        return;
    }

    // Sort by due date
    sortArr(a, n, byDue);

    long t = dayNum(today());

    say(fmt("%-6s %-20s %13s  %-10s  %-16s",
            "ID", "Name", "Amount", "Due", "Status"));

    say(rep(72, '-'));

    double total = 0;

    for (int i = 0; i < n; i++) {
        Person *p = a[i];

        long diff = dayNum(p->due) - t;

        // Determine payment status
        string st = diff < 0 ?
                    fmt("OVERDUE %ld day%s",
                        -diff, diff == -1 ? "" : "s") :

                    diff == 0 ?
                    string("Due today") :

                    fmt("In %ld day%s",
                        diff, diff == 1 ? "" : "s");

        total -= p->bal;

        say(fmt("%-6d %-20.20s ", p->id, p->name.c_str()) +
            tag(-1, fmt("%13.2f", -p->bal)) +
            fmt("  %-10s  %-16s",
                dateStr(p->due).c_str(), st.c_str()));
    }

    say(rep(72, '-'));

    say("Total pending: " + tag(-1, money(total)));

    delete[] a;
}


// ============================================================
// 15. COMMAND HANDLER
// ============================================================

// Connect button IDs to their corresponding functions
bool runCommand(int id) {
    switch (id) {
        case ID_ADD:   doAdd(); break;
        case ID_VIEW:  doView(); break;
        case ID_SID:   doSearchId(); break;
        case ID_SNAME: doSearchName(); break;
        case ID_GAVE:  doTxn(true); break;
        case ID_GOT:   doTxn(false); break;
        case ID_HIST:  doHistory(); break;
        case ID_ENAME: doEditName(); break;
        case ID_EDUE:  doEditDue(); break;
        case ID_DEL:   doDelete(); break;
        case ID_UNDO:  doUndo(); break;
        case ID_REDO:  doRedo(); break;
        case ID_DASH:  doDash(); break;
        case ID_SBAL:  doSorted(1); break;
        case ID_SNAM:  doSorted(2); break;
        case ID_SID2:  doSorted(3); break;
        case ID_DUE:   doDue(); break;

        default:
            return false;
    }

    return true;
}


// ============================================================
// 16. WINDOW AND INPUT CONTROL
// ============================================================

// Determine which input boxes are needed for each command
int needMask(int id) {
    const int I = 1 << E_ID;
    const int N = 1 << E_NAME;
    const int A = 1 << E_AMT;
    const int D = 1 << E_DUE;

    switch (id) {
        case ID_ADD:
            return N | A | D;

        case ID_SID:
        case ID_DEL:
        case ID_HIST:
            return I;

        case ID_SNAME:
            return N;

        case ID_ENAME:
            return I | N;

        case ID_EDUE:
            return I | D;

        case ID_GAVE:
        case ID_GOT:
            return I | A;
    }

    return 0;
}

// Clear all input boxes
void clearInputs() {
    for (int i = 0; i < 4; i++)
        SetWindowTextA(ed[i], "");
}

// Helper to create GUI controls
HWND mkCtl(HWND parent, const char *cls,
           const char *text, DWORD style,
           HFONT font = 0, int id = 0) {

    HWND c = CreateWindowA(
        cls, text,
        WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10,
        parent, (HMENU)(INT_PTR)id, 0, 0
    );

    // Apply font if provided
    if (font)
        SendMessageA(c, WM_SETFONT, (WPARAM)font, 1);

    return c;
}


// ============================================================
// 17. GUI LAYOUT
// ============================================================

// Position controls according to screen size
void layout(HWND h) {
    if (!eOut)
        return;

    RECT rc;
    GetClientRect(h, &rc);

    int cw = rc.right;
    int ch = rc.bottom;

    const int M = 20, G = 20;

    int bh = ch < 800 ? 30 : 38;      // Button height
    int step = bh + 6;

    // Header positions
    MoveWindow(hTitle, 0, 8, cw, 44, TRUE);
    MoveWindow(hSub, 0, 52, cw, 26, TRUE);
    MoveWindow(hExit, cw - M - 110, 14, 110, 34, TRUE);

    // Calculate four column widths
    int colw = (cw - 2 * M - 3 * G) / 4;

    int top = HDR;
    int row[4] = {0, 0, 0, 0};

    // Position section headings
    for (int i = 0; i < 4; i++)
        MoveWindow(hSec[i],
                   M + i * (colw + G),
                   top, colw, 26, TRUE);

    // Position action buttons
    for (int i = 0; i < NBTN; i++) {
        int s = btns[i].sec;

        MoveWindow(hBtn[i],
                   M + s * (colw + G),
                   top + 32 + row[s] * step,
                   colw, bh, TRUE);

        row[s]++;
    }

    // Find maximum number of buttons in a column
    int maxRows = 0;

    for (int i = 0; i < 4; i++)
        if (row[i] > maxRows)
            maxRows = row[i];

    // Input section position
    int entryY = top + 32 + maxRows * step + 10;
    int outY = entryY;

    // Get input requirements
    int mask = pending ? needMask(pending) : 0;

    int cnt = 0;

    for (int i = 0; i < 4; i++)
        if ((mask >> i) & 1)
            cnt++;

    const int BW = 110;
    int fw = 0;

    if (cnt) {
        fw = (cw - 2 * M - 2 * BW - 2 * G -
              (cnt - 1) * G) / cnt;

        if (fw > 400)
            fw = 400;
    }

    int x = M;

    // Show only required input boxes
    for (int i = 0; i < 4; i++) {
        bool on = ((mask >> i) & 1) != 0;

        ShowWindow(hLbl[i], on ? SW_SHOW : SW_HIDE);
        ShowWindow(ed[i], on ? SW_SHOW : SW_HIDE);

        if (on) {
            MoveWindow(hLbl[i], x, entryY, fw, 20, TRUE);

            MoveWindow(ed[i], x, entryY + 22, fw, 28, TRUE);

            x += fw + G;
        }
    }

    // Show Submit and Cancel when input is required
    ShowWindow(hOk, cnt ? SW_SHOW : SW_HIDE);
    ShowWindow(hCancel, cnt ? SW_SHOW : SW_HIDE);

    if (cnt) {
        MoveWindow(hOk, x, entryY + 22, BW, 28, TRUE);

        MoveWindow(hCancel, x + BW + G,
                   entryY + 22, BW, 28, TRUE);
    }

    // Show hints for relevant commands
    bool showHint = (pending == ID_ADD ||
                     pending == ID_GAVE ||
                     pending == ID_GOT);

    ShowWindow(hHint, showHint ? SW_SHOW : SW_HIDE);

    if (showHint)
        MoveWindow(hHint, M, entryY + 56,
                   cw - 2 * M, 20, TRUE);

    if (cnt)
        outY += 84;

    // Output pane and colour key
    if (ch - outY - M > 60) {

        MoveWindow(eOut, M, outY,
                   cw - 2 * M - KEYW - G,
                   ch - outY - M, TRUE);

        int kx = cw - M - KEYW;

        keyRc.left = kx;
        keyRc.top = outY;
        keyRc.right = kx + KEYW;
        keyRc.bottom = outY + 150;

        MoveWindow(hKey[0], kx + 10,
                   outY + 10, KEYW - 20, 28, TRUE);

        MoveWindow(hKey[1], kx + 46,
                   outY + 52, KEYW - 56, 28, TRUE);

        MoveWindow(hKey[2], kx + 46,
                   outY + 90, KEYW - 56, 28, TRUE);
    }

    InvalidateRect(h, 0, TRUE);
}


// ============================================================
// 18. WINDOW PROCEDURE - MAIN GUI EVENT HANDLER
// ============================================================

// Receives and processes Windows messages
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {

    switch (m) {

        // ----------------------------------------------------
        // WINDOW CREATION
        // ----------------------------------------------------
        case WM_CREATE: {
            hMain = h;

            LoadLibraryA("Riched20.dll");   // Load Rich Edit library

            // Create fonts
            fBig = CreateFontA(
                -40, 0, 0, 0, FW_BOLD, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, "Georgia"
            );

            fSub = CreateFontA(
                -22, 0, 0, 0, FW_NORMAL, TRUE, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, "Georgia"
            );

            fMid = CreateFontA(
                -20, 0, 0, 0, FW_BOLD, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, "Trebuchet MS"
            );

            fBtn = CreateFontA(
                -18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, 0, 0, "Calibri"
            );

            fMono = CreateFontA(
                -17, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                FIXED_PITCH | FF_MODERN, "Consolas"
            );

            // Create title and subtitle
            hTitle = mkCtl(h, "STATIC",
                           "W H O   O W E S   W H O",
                           SS_CENTER, fBig);

            hSub = mkCtl(h, "STATIC",
                         "I   O W E   Y O U",
                         SS_CENTER, fSub);

            // Create owner-drawn buttons
            hExit = mkCtl(h, "BUTTON", "Exit",
                          WS_TABSTOP | BS_OWNERDRAW, 0, ID_EXIT);

            hOk = mkCtl(h, "BUTTON", "Submit",
                        WS_TABSTOP | BS_OWNERDRAW, 0, ID_OK);

            hCancel = mkCtl(h, "BUTTON", "Cancel",
                            WS_TABSTOP | BS_OWNERDRAW, 0, ID_CANCEL);

            // Input box labels
            const char *lt[4] = {
                "ID", "Name", "Amount", "Due (DD-MM-YYYY)"
            };

            // Create four input boxes
            for (int i = 0; i < 4; i++) {

                hLbl[i] = mkCtl(h, "STATIC", lt[i], 0, fBtn);

                ed[i] = mkCtl(
                    h, "EDIT", "",
                    WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                    fBtn
                );
            }

            // Hint text
            hHint = mkCtl(h, "STATIC", "", 0, fBtn);

            // Section headings
            for (int i = 0; i < 4; i++)
                hSec[i] = mkCtl(h, "STATIC",
                                secNames[i], SS_CENTER, fMid);

            // Create 17 action buttons
            for (int i = 0; i < NBTN; i++)
                hBtn[i] = mkCtl(
                    h, "BUTTON", btns[i].t,
                    WS_TABSTOP | BS_OWNERDRAW,
                    0, btns[i].id
                );

            // Rich Edit output area
            eOut = mkCtl(
                h, "RichEdit20A", "",
                WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                ES_MULTILINE | ES_READONLY |
                ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                fMono
            );

            SendMessageA(eOut, EM_SETBKGNDCOLOR,
                         0, (LPARAM)C_CREAM);

            // Colour key
            hKey[0] = mkCtl(h, "STATIC", "- KEY -",
                            SS_CENTER, fMid);

            hKey[1] = mkCtl(h, "STATIC", "They owe you",
                            0, fMid);

            hKey[2] = mkCtl(h, "STATIC", "You owe them",
                            0, fMid);

            // Timer checks mouse hover every 60 ms
            SetTimer(h, 1, 60, 0);

            // Initial output
            say("Choose a command above.");

            if (personCount > 0)
                say("Loaded " + str(personCount) +
                    " record(s) from saved data.");

            say("Fill the boxes above, then press a button.");

            flush();
            layout(h);

            return 0;
        }

        // ----------------------------------------------------
        // WINDOW RESIZED
        // ----------------------------------------------------
        case WM_SIZE:
            layout(h);
            return 0;

        // ----------------------------------------------------
        // DRAW COLOUR KEY
        // ----------------------------------------------------
        case WM_PAINT: {
            PAINTSTRUCT ps;

            HDC dc = BeginPaint(h, &ps);

            if (keyRc.right > keyRc.left) {

                // Create cream card and green border
                HBRUSH cb = CreateSolidBrush(C_CREAM);
                HPEN cp = CreatePen(PS_SOLID, 3, C_GREEN);

                HBRUSH ob = (HBRUSH)SelectObject(dc, cb);
                HPEN op = (HPEN)SelectObject(dc, cp);

                RoundRect(dc, keyRc.left, keyRc.top,
                          keyRc.right, keyRc.bottom, 16, 16);

                SelectObject(dc, ob);
                SelectObject(dc, op);

                DeleteObject(cb);
                DeleteObject(cp);

                // Draw green and red colour boxes
                int sx = keyRc.left + 14;

                int sy[2] = {
                    keyRc.top + 54,
                    keyRc.top + 92
                };

                COLORREF sc[2] = {
                    C_OWED_TO_ME,
                    C_I_OWE
                };

                for (int i = 0; i < 2; i++) {
                    HBRUSH sb = CreateSolidBrush(sc[i]);

                    RECT sr = {
                        sx, sy[i], sx + 24, sy[i] + 24
                    };

                    FillRect(dc, &sr, sb);
                    FrameRect(dc, &sr, brDark);

                    DeleteObject(sb);
                }
            }

            EndPaint(h, &ps);
            return 0;
        }

        // ----------------------------------------------------
        // BUTTON HOVER TIMER
        // ----------------------------------------------------
        case WM_TIMER: {
            POINT pt;

            GetCursorPos(&pt);          // Mouse screen position

            HWND c = WindowFromPoint(pt); // Control under mouse

            bool ours = (c == hExit || c == hOk || c == hCancel);

            for (int i = 0; i < NBTN; i++)
                if (c == hBtn[i])
                    ours = true;

            if (!ours)
                c = 0;

            // Redraw old and new hovered buttons
            if (c != hHot) {
                HWND old = hHot;

                hHot = c;

                if (old)
                    InvalidateRect(old, 0, TRUE);

                if (c)
                    InvalidateRect(c, 0, TRUE);
            }

            return 0;
        }

        // ----------------------------------------------------
        // INPUT BOX COLOURS
        // ----------------------------------------------------
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)w;

            // Check whether this box has focus
            bool focused = ((HWND)l == GetFocus());

            SetTextColor(dc, C_DARK);

            SetBkColor(dc, focused ? C_CREAM : C_LIME);

            return (LRESULT)(focused ? brCream : brLime);
        }

        // ----------------------------------------------------
        // STATIC CONTROL COLOURS
        // ----------------------------------------------------
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w;
            HWND c = (HWND)l;

            // Output and key use cream background
            if (c == eOut) {
                SetTextColor(dc, C_DARK);
                SetBkColor(dc, C_CREAM);

                return (LRESULT)brCream;
            }

            // Colour key text
            if (c == hKey[0] ||
                c == hKey[1] ||
                c == hKey[2]) {

                SetTextColor(
                    dc,
                    c == hKey[1] ? C_OWED_TO_ME :
                    c == hKey[2] ? C_I_OWE : C_DARK
                );

                SetBkColor(dc, C_CREAM);

                return (LRESULT)brCream;
            }

            // Other static controls
            bool accent = (c == hSub || c == hHint);

            for (int i = 0; i < 4; i++)
                if (c == hSec[i])
                    accent = true;

            SetTextColor(dc, accent ? C_LIME : C_CREAM);
            SetBkColor(dc, C_DARK);

            return (LRESULT)brDark;
        }

        // ----------------------------------------------------
        // CUSTOM BUTTON DRAWING
        // ----------------------------------------------------
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;

            if (d->CtlType != ODT_BUTTON)
                break;

            // Check button state
            bool down = (d->itemState & ODS_SELECTED) != 0;
            bool hot = (d->hwndItem == hHot);

            // Background behind rounded corners
            FillRect(d->hDC, &d->rcItem, brDark);

            // Choose button colour
            COLORREF fill = down ? C_LIME :
                            hot ? RGB(0x57, 0x96, 0x72) :
                            C_GREEN;

            COLORREF edge = (down || hot) ? C_LIME : C_CREAM;

            HBRUSH nb = CreateSolidBrush(fill);
            HPEN np = CreatePen(PS_SOLID, 2, edge);

            HBRUSH ob = (HBRUSH)SelectObject(d->hDC, nb);
            HPEN op = (HPEN)SelectObject(d->hDC, np);

            // Draw rounded button
            RoundRect(
                d->hDC,
                d->rcItem.left + 1,
                d->rcItem.top + 1,
                d->rcItem.right - 1,
                d->rcItem.bottom - 1,
                14, 14
            );

            SelectObject(d->hDC, ob);
            SelectObject(d->hDC, op);

            DeleteObject(nb);
            DeleteObject(np);

            // Get button text
            char t[64];

            GetWindowTextA(d->hwndItem, t, sizeof t);

            RECT tr = d->rcItem;

            if (down)
                OffsetRect(&tr, 1, 1);

            SetBkMode(d->hDC, TRANSPARENT);

            SetTextColor(d->hDC, down ? C_DARK : C_CREAM);

            HFONT old = (HFONT)SelectObject(d->hDC, fBtn);

            // Draw centered button text
            DrawTextA(
                d->hDC, t, -1, &tr,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE
            );

            SelectObject(d->hDC, old);

            // Draw focus rectangle
            if (d->itemState & ODS_FOCUS) {
                RECT r = d->rcItem;

                InflateRect(&r, -5, -5);
                DrawFocusRect(d->hDC, &r);
            }

            return TRUE;
        }

        // ----------------------------------------------------
        // BUTTON CLICKS AND COMMAND EXECUTION
        // ----------------------------------------------------
        case WM_COMMAND: {

            // Input box focus changes
            if (HIWORD(w) == EN_SETFOCUS ||
                HIWORD(w) == EN_KILLFOCUS) {

                InvalidateRect((HWND)l, 0, TRUE);
                return 0;
            }

            // Ignore anything that is not a button click
            if (HIWORD(w) != BN_CLICKED)
                return 0;

            // Extract clicked button ID
            int cid = LOWORD(w);

            // EXIT
            if (cid == ID_EXIT) {
                PostMessageA(h, WM_CLOSE, 0, 0);
                return 0;
            }

            // CANCEL
            if (cid == ID_CANCEL) {
                pending = 0;

                clearInputs();
                layout(h);

                return 0;
            }

            // SUBMIT
            if (cid == ID_OK) {

                // Get previously selected command
                cid = pending;

                if (!cid)
                    return 0;

                pending = 0;

                layout(h);
            }

            // Commands that require input
            else if (needMask(cid)) {

                pending = cid;          // Store selected command

                clearInputs();

                // Set relevant input hint
                SetWindowTextA(
                    hHint,
                    cid == ID_ADD ?
                    "Amount: negative = they owe you, positive = you owe them (opening balance)." :
                    "Amount: enter a positive number."
                );

                layout(h);

                // Focus first required input box
                for (int i = 0; i < 4; i++)
                    if ((needMask(cid) >> i) & 1) {
                        SetFocus(ed[i]);
                        break;
                    }

                return 0;
            }

            // Commands without inputs
            else {
                pending = 0;
                layout(h);
            }

            // Execute selected command
            if (runCommand(cid)) {

                flush();               // Display result

                saveData();             // Save updated records

                clearInputs();          // Clear input fields
            }

            return 0;
        }

        // ----------------------------------------------------
        // CLOSE WINDOW
        // ----------------------------------------------------
        case WM_CLOSE:
            saveData();                 // Save before closing
            DestroyWindow(h);           // Destroy main window
            return 0;

        // ----------------------------------------------------
        // DESTROY WINDOW
        // ----------------------------------------------------
        case WM_DESTROY:
            KillTimer(h, 1);            // Stop hover timer

            PostQuitMessage(0);         // Stop message loop

            return 0;
    }

    // Default handling for unprocessed messages
    return DefWindowProcA(h, m, w, l);
}


// ============================================================
// 19. MAIN ENTRY POINT - WINMAIN
// ============================================================

// Entry point of a Windows GUI application
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {

    // Initialize all hash table buckets
    for (int i = 0; i < HS; i++)
        table[i] = 0;

    loadData();                         // Restore saved records

    // Create background brushes
    brDark = CreateSolidBrush(C_DARK);
    brLime = CreateSolidBrush(C_LIME);
    brCream = CreateSolidBrush(C_CREAM);

    // Define main window class
    WNDCLASSA wc = {};

    wc.lpfnWndProc = WndProc;           // Message handler
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = brDark;
    wc.lpszClassName = "WowGui";

    RegisterClassA(&wc);                // Register window class

    // Create fullscreen borderless window
    HWND w = CreateWindowA(
        "WowGui",
        "WHO OWES WHO - I OWE YOU",
        WS_POPUP,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        0, 0, hi, 0
    );

    ShowWindow(w, SW_SHOW);             // Display window

    SetForegroundWindow(w);             // Bring to front

    MSG msg;

    // MAIN WINDOWS MESSAGE LOOP
    while (GetMessageA(&msg, 0, 0, 0) > 0) {

        // Escape key closes application
        if (msg.message == WM_KEYDOWN &&
            msg.wParam == VK_ESCAPE) {

            PostMessageA(w, WM_CLOSE, 0, 0);
            continue;
        }

        // Handle keyboard navigation
        if (!IsDialogMessageA(w, &msg)) {

            TranslateMessage(&msg);     // Translate keyboard input

            DispatchMessageA(&msg);     // Send message to WndProc
        }
    }

    return 0;                           // End application
}
