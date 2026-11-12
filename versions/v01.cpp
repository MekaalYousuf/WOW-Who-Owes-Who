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

