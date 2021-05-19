#ifndef _GENSFEN_NONPV_H_
#define _GENSFEN_NONPV_H_

#include <sstream>

namespace Stockfish::Learner {

    // Automatic generation of teacher position
    void gensfen_nonpv(std::istringstream& is);
}

#endif
