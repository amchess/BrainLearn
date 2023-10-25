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

#include <cstddef>
#include <iostream>

#include "bitboard.h"
#include "evaluate.h"
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tune.h"
#include "types.h"
#include "uci.h"
#include "learn.h"      //learning
#include "book/book.h"  //book
using namespace Brainlearn;

int main(int argc, char* argv[]) {

    std::cout << engine_info() << std::endl;

    CommandLine::init(argc, argv);
    Utility::init(argv[0]);  //Khalid
    UCI::init(Options);
    Tune::init();
    LD.init();  //Kelly
    Bitboards::init();
    Position::init();
    Threads.set(size_t(Options["Threads"]));
    Search::clear();  // After threads are up
    Eval::NNUE::init();
    Book::init();  //Books management

    UCI::loop(argc, argv);

    Threads.set(0);
    return 0;
}
