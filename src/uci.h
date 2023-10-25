/*
  Brainlearn, a UCI chess playing engine derived from Brainlearn
  Copyright (C) 2004-2023 Andrea Manzo, K.Kiniama and Brainlearn developers (see AUTHORS file)

  Brainlearn is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Brainlearn is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>

#include "types.h"

namespace Brainlearn {

class Position;

namespace UCI {

// Normalizes the internal value as reported by evaluate or search
// to the UCI centipawn result used in output. This value is derived from
// the win_rate_model() such that Brainlearn outputs an advantage of
// "100 centipawns" for a position if the engine has a 50% probability to win
// from this position in selfplay at fishtest LTC time control.
const int NormalizeToPawnValue = 328;

class Option;

// Define a custom comparator, because the UCI options should be case-insensitive
struct CaseInsensitiveLess {
    bool operator()(const std::string&, const std::string&) const;
};

// The options container is defined as a std::map
using OptionsMap = std::map<std::string, Option, CaseInsensitiveLess>;

// The Option class implements each option as specified by the UCI protocol
class Option {

    using OnChange = void (*)(const Option&);

   public:
    Option(OnChange = nullptr);
    Option(bool v, OnChange = nullptr);
    Option(const char* v, OnChange = nullptr);
    Option(double v, int minv, int maxv, OnChange = nullptr);
    Option(const char* v, const char* cur, OnChange = nullptr);

    Option& operator=(const std::string&);
    void    operator<<(const Option&);
    operator int() const;
    operator std::string() const;
    bool operator==(const char*) const;

   private:
    friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

    std::string defaultValue, currentValue, type;
    int         min, max;
    size_t      idx;
    OnChange    on_change;
};

void        init(OptionsMap&);
void        loop(int argc, char* argv[]);
int         to_cp(Value v);
std::string value(Value v);
std::string square(Square s);
std::string move(Move m, bool chess960);
std::string pv(const Position& pos, Depth depth);
std::string wdl(Value v, int ply);
Move        to_move(const Position& pos, std::string& str);

}  // namespace UCI

extern UCI::OptionsMap Options;

}  // namespace Brainlearn

#endif  // #ifndef UCI_H_INCLUDED
