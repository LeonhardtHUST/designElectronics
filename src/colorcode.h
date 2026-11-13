// designElectronics — resistor colour code and component marking codes.
//
// Pure logic, no UI and no SDL: the part generators (partlib.cpp), the colour code
// calculator tool and the self-test all share it, so the bands drawn on a resistor
// and the numbers the calculator shows can never disagree.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "geom.h"

namespace cc {

// Band colours. 0..9 are the digit colours, gold and silver are the sub-unit
// multipliers / wide tolerances. NONE marks "no band here".
enum Color { BLACK, BROWN, RED, ORANGE, YELLOW, GREEN, BLUE, VIOLET, GREY, WHITE, GOLD, SILVER, NONE, COLOR_COUNT };

uint32_t colorRgba(int c);
const char* colorKey(int c);  // "black", "brown", … (translation key suffix: cc.color.<key>)

enum Role { DIGIT, MULTIPLIER, TOLERANCE, TEMPCO };

// What a colour code stands for.
struct Spec {
    int bands = 4;          // 3, 4, 5 or 6
    double ohms = 1000;
    double tolerance = 5;   // percent; 3-band resistors are always 20 %
    int ppm = 100;   // temperature coefficient (6 bands only)
};

struct Bands {
    int n = 0;
    int color[6] = {NONE, NONE, NONE, NONE, NONE, NONE};
    Role role[6] = {DIGIT, DIGIT, DIGIT, DIGIT, DIGIT, DIGIT};
    int digits() const { return n >= 5 ? 3 : 2; }  // significant digits
};

// The bands that show `s`. `exact` (optional) is false when the value cannot be
// written with that many bands (4.75 kΩ on 4 bands) and has been rounded.
Bands encode(const Spec& s, bool* exact = nullptr);
// The value the bands stand for. Returns false for a band set that is not a valid code.
bool decode(const Bands& b, Spec& out);

// Where the bands sit on a resistor body, as fractions of the body length: x[i] is
// the left edge of band i, `width` the width every band shares. The digit and
// multiplier bands are grouped on the left, tolerance (and temperature coefficient)
// are set apart on the right, as on a real part.
void bandLayout(int n, float x[6], float& width);

// Colours a band of that role may take, in palette order.
std::vector<int> allowedColors(Role r, int bands);
// "1", "×1k", "±5%", "50 ppm/°C": what a colour means in that role.
std::string meaning(Role r, int color);

const std::vector<double>& toleranceChoices();  // percent
const std::vector<int>& tempcoChoices();        // ppm/°C
double snapTolerance(double pct);
int snapTempco(int ppm);
double parseTolerance(const std::string& s, double def = 5);  // "±0.5%" -> 0.5
std::string formatTolerance(double pct);         // 0.5 -> "±0.5%"
int parsePpm(const std::string& s, int def = 100);      // "50ppm" -> 50
std::string formatPpm(int ppm);    // 50 -> "50ppm"

std::string formatOhms(double ohms);  // 4700 -> "4.7kΩ"
// "4.7k", "4k7", "4R7", "R47", "100" -> ohms; `def` when it is not a number
double parseResistance(const std::string& text, double def = -1);

// SMD / printed markings, so the text on a part follows its value.
//   resistor: "103" (3 digits, ±2 % and worse), "1002" (4 digits, ±1 % and better), "4R7" below 10 Ω
//   capacitor: "104" (EIA, picofarads × multiplier), "22" / "4p7" below 100 pF
std::string resistorMarking(double ohms, double tolerancePct);
std::string capacitorMarking(double farads);

// State of one calculator widget (the standalone tool and the Inspector each own one).
struct CalcState {
    Spec spec;
    std::string valueText = "1kΩ";  // what the resistance field shows
    int sel = 0;      // band whose palette is open
};

// A resistor part's properties <-> a Spec (props: resistance, bands, tolerance, tempco).
Spec specFromProps(const std::map<std::string, std::string>& props);
std::map<std::string, std::string> propsFromSpec(const Spec& s);
bool sameSpec(const Spec& a, const Spec& b);

}  // namespace cc
