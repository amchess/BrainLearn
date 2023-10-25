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

#include "search.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "bitboard.h"
#include <fstream>  // kelly
#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/evaluate_nnue.h"
#include "nnue/nnue_common.h"
#include "position.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "learn.h"  //Khalid
#include "uci.h"
#include "mcts/montecarlo.h"  //Montecarlo
#include "book/book.h"        //books management

namespace Brainlearn {
// livebook begin
#ifdef USE_LIVEBOOK
    #define CURL_STATICLIB
extern "C" {
    #include <curl/curl.h>
}
    #undef min
    #undef max
#endif
// livebook end

// Kelly begin
bool                               useLearning          = true;
bool                               enabledLearningProbe = false;
std::vector<PersistedLearningMove> gameLine;
// Kelly end

namespace Search {

LimitsType Limits;
}

namespace Tablebases {

int   Cardinality;
bool  RootInTB;
bool  UseRule50;
Depth ProbeDepth;
}

namespace TB = Tablebases;

using std::string;
using Eval::evaluate;
using namespace Search;

namespace {

// Different node types, used as a template parameter
enum NodeType {
    NonPV,
    PV,
    Root
};

// Futility margin
Value futility_margin(Depth d, bool noTtCutNode, bool improving) {
    return Value((126 - 42 * noTtCutNode) * (d - improving));
}

// Reductions lookup table initialized at startup
int Reductions[MAX_MOVES];  // [depth or moveNumber]

Depth reduction(bool i, Depth d, int mn, Value delta, Value rootDelta) {
    int reductionScale = Reductions[d] * Reductions[mn];
    if (rootDelta != 0)
        return (reductionScale + 1560 - int(delta) * 945 / int(rootDelta)) / 1024
             + (!i && reductionScale > 791);
    else  // avoid divide by zero error
        return (reductionScale + 1560 - int(delta) * 945) / 1024 + (!i && reductionScale > 791);
}

constexpr int futility_move_count(bool improving, Depth depth) {
    return improving ? (3 + depth * depth) : (3 + depth * depth) / 2;
}

// History and stats update bonus, based on depth
int stat_bonus(Depth d) { return std::min(334 * d - 531, 1538); }

// Add a small random component to draw evaluations to avoid 3-fold blindness
Value value_draw(const Thread* thisThread) {
    return VALUE_DRAW - 1 + Value(thisThread->nodes & 0x2);
}

// Skill structure is used to implement strength limit. If we have a UCI_Elo,
// we convert it to an appropriate skill level, anchored to the Stash engine.
// This method is based on a fit of the Elo results for games played between
// Brainlearn at various skill levels and various versions of the Stash engine.
// Skill 0 .. 19 now covers CCRL Blitz Elo from 1320 to 3190, approximately
// Reference: https://github.com/vondele/Brainlearn/commit/a08b8d4e9711c2
struct Skill {
    Skill(int skill_level, int uci_elo) {
        if (uci_elo)
        {
            double e = double(uci_elo - 1320) / (3190 - 1320);
            level = std::clamp((((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438), 0.0, 19.0);
        }
        else
            level = double(skill_level);
    }
    bool enabled() const { return level < 20.0; }
    bool time_to_pick(Depth depth) const { return depth == 1 + int(level); }
    Move pick_best(size_t multiPV);

    double level;
    Move   best = MOVE_NONE;
};

int openingVariety;  // from Sugar

template<NodeType nodeType>
Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);

template<NodeType nodeType>
Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth = 0);

Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply, int r50c);
void  update_pv(Move* pv, Move move, const Move* childPv);
void  update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);
void  update_quiet_stats(const Position& pos, Stack* ss, Move move, int bonus);
void  update_all_stats(const Position& pos,
                       Stack*          ss,
                       Move            bestMove,
                       Value           bestValue,
                       Value           beta,
                       Square          prevSq,
                       Move*           quietsSearched,
                       int             quietCount,
                       Move*           capturesSearched,
                       int             captureCount,
                       Depth           depth);

// Utility to verify move generation. All the leaf nodes up
// to the given depth are generated and counted, and the sum is returned.
template<bool Root>
uint64_t perft(Position& pos, Depth depth) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    uint64_t   cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<LEGAL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m, st);
            cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }
        if (Root)
            sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
    }
    return nodes;
}

}  // namespace


// livebook begin
#ifdef USE_LIVEBOOK
CURL*       g_cURL;
std::string g_szRecv;
std::string g_livebookURL = "http://www.chessdb.cn/cdb.php";
int         g_inBook;
int         livebook_depth_count = 0;
int         max_book_depth;

size_t cURL_WriteFunc(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try
    { s->append((char*) contents, newLength); } catch (std::bad_alloc&)
    {
        // handle memory problem
        return 0;
    }
    return newLength;
}
void Search::setLiveBookURL(const std::string& newURL) { g_livebookURL = newURL; }
void Search::setLiveBookTimeout(size_t newTimeoutMS) {
    curl_easy_setopt(g_cURL, CURLOPT_TIMEOUT_MS, newTimeoutMS);
}
void Search::set_livebook_retry(int retry) { g_inBook = retry; }
void Search::set_livebook_depth(int book_depth) { max_book_depth = book_depth; }
#endif
// livebook end
// Called at startup to initialize various lookup tables
void Search::init() {

    for (int i = 1; i < MAX_MOVES; ++i)
        Reductions[i] = int((20.37 + std::log(Threads.size()) / 2) * std::log(i));

// livebook begin
#ifdef USE_LIVEBOOK
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_cURL = curl_easy_init();
    curl_easy_setopt(g_cURL, CURLOPT_TIMEOUT_MS, 1000L);
    curl_easy_setopt(g_cURL, CURLOPT_WRITEFUNCTION, cURL_WriteFunc);
    curl_easy_setopt(g_cURL, CURLOPT_WRITEDATA, &g_szRecv);
    set_livebook_retry((int) Options["Live Book Retry"]);
    set_livebook_depth((int) Options["Live Book Depth"]);
#endif
    // livebook end
}


// Resets search state to its initial value
void Search::clear() {

    Threads.main()->wait_for_search_finished();
// livebook begin
#ifdef USE_LIVEBOOK
    set_livebook_retry((int) Options["Live Book Retry"]);
    set_livebook_depth((int) Options["Live Book Depth"]);
#endif
    // livebook end
    Time.availableNodes = 0;
    TT.clear();
    MCTS.clear();  // mcts
    Threads.clear();
    Tablebases::init(Options["SyzygyPath"]);  // Free mapped files
}
//for mcts begin
inline Value static_value(Position& pos, Stack* ss) {
    // Check if MAX_PLY is reached
    if (ss->ply >= MAX_PLY)
        return VALUE_DRAW;

    // Check for immediate draw
    if (pos.is_draw(ss->ply) && !pos.checkers())
        return VALUE_DRAW;

    // Detect mate and stalimate situations
    if (MoveList<LEGAL>(pos).size() == 0)
        return pos.checkers() ? VALUE_MATE : VALUE_DRAW;

    // Evaluate the position statically
    return evaluate(pos);
}
//for mcts end

// Called when the program receives the UCI 'go'
// command. It searches from the root position and outputs the "bestmove".
void MainThread::search() {

    if (Limits.perft)
    {
        nodes = perft<true>(rootPos, Limits.perft);
        sync_cout << "\nNodes searched: " << nodes << "\n" << sync_endl;
        return;
    }

    Color us = rootPos.side_to_move();
    Time.init(Limits, us, rootPos.game_ply());
    TT.new_search();
    // Kelly begin
    enabledLearningProbe = false;
    useLearning          = true;
    // Kelly end

    openingVariety = Options["Opening variety"];  // from Sugar
    Eval::NNUE::verify();
    Move bookMove = MOVE_NONE;  //Books management

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(MOVE_NONE);
        sync_cout << "info depth 0 score "
                  << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW) << sync_endl;
    }
    else
    // Books management begin
    {

        bool think = true;
        if (!Limits.infinite && !Limits.mate)
        {
            if (!Limits.depth && !Limits.nodes && !Limits.perft && !ponder)
            {
                //Probe the configured books
                bookMove = Book::probe(rootPos);
                if (bookMove != MOVE_NONE
                    && std::find(rootMoves.begin(), rootMoves.end(), bookMove) != rootMoves.end())
                {
                    think = false;

                    for (Thread* th : Threads)
                        std::swap(th->rootMoves[0],
                                  *std::find(th->rootMoves.begin(), th->rootMoves.end(), bookMove));
                }
            }
// Live Book begin
#ifdef USE_LIVEBOOK
            if (think)
            {
                if (!bookMove)
                {
                    if (Options["Live Book"] && g_inBook)
                    {
                        if (rootPos.game_ply() == 0)
                            livebook_depth_count = 0;
                        if (livebook_depth_count < max_book_depth)
                        {
                            CURLcode    res;
                            char*       szFen = curl_easy_escape(g_cURL, rootPos.fen().c_str(), 0);
                            std::string szURL =
                              g_livebookURL + "?action="
                              + (Options["Live Book Diversity"] ? "query" : "querybest")
                              + "&board=" + szFen;
                            curl_free(szFen);
                            curl_easy_setopt(g_cURL, CURLOPT_URL, szURL.c_str());
                            g_szRecv.clear();
                            res = curl_easy_perform(g_cURL);
                            if (res == CURLE_OK)
                            {
                                g_szRecv.erase(std::find(g_szRecv.begin(), g_szRecv.end(), '\0'),
                                               g_szRecv.end());
                                if (g_szRecv.find("move:") != std::string::npos)
                                {
                                    std::string tmp = g_szRecv.substr(5);
                                    bookMove        = UCI::to_move(rootPos, tmp);
                                    livebook_depth_count++;
                                }
                            }
                        }
                        if (bookMove && std::count(rootMoves.begin(), rootMoves.end(), bookMove))
                        {
                            g_inBook = Options["Live Book Retry"];
                            think    = false;
                            for (Thread* th : Threads)
                                std::swap(
                                  th->rootMoves[0],
                                  *std::find(th->rootMoves.begin(), th->rootMoves.end(), bookMove));
                        }
                        else
                        {
                            bookMove = MOVE_NONE;
                            g_inBook--;
                        }
                    }
                }
            }
#endif
            // Live Book end
        }

        if (!bookMove || think)
        {
            Threads.start_searching();  // start non-main threads
            Thread::search();           // main thread start searching
        }
    }
    // Books management end

    // When we reach the maximum depth, we can arrive here without a raise of
    // Threads.stop. However, if we are pondering or in an infinite search,
    // the UCI protocol states that we shouldn't print the best move before the
    // GUI sends a "stop" or "ponderhit" command. We therefore simply wait here
    // until the GUI sends one of those commands.

    while (!Threads.stop && (ponder || Limits.infinite))
    {}  // Busy wait for a stop or a ponder reset

    // Stop the threads if not already stopped (also raise the stop if
    // "ponderhit" just reset Threads.ponder).
    Threads.stop = true;

    // Wait until all threads have finished
    Threads.wait_for_search_finished();

    // When playing in 'nodes as time' mode, subtract the searched nodes from
    // the available ones before exiting.
    if (Limits.npmsec)
        Time.availableNodes += Limits.inc[us] - Threads.nodes_searched();

    Thread* bestThread = this;
    Skill   skill =
      Skill(Options["Skill Level"], Options["UCI_LimitStrength"] ? int(Options["UCI_Elo"]) : 0);

    if (int(Options["MultiPV"]) == 1 && !Limits.depth && !skill.enabled()
        && rootMoves[0].pv[0] != MOVE_NONE)
        bestThread = Threads.get_best_thread();

    bestPreviousScore        = bestThread->rootMoves[0].score;
    bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

    // kelly begin
    if (!bookMove)
    {
        if (bestThread->completedDepth > 4 && LD.is_enabled() && !LD.is_paused())  // from Khalid
        {
            PersistedLearningMove plm;
            plm.key                = rootPos.key();
            plm.learningMove.depth = bestThread->completedDepth;
            plm.learningMove.move  = bestThread->rootMoves[0].pv[0];
            plm.learningMove.score = bestThread->rootMoves[0].score;
            if (LD.learning_mode() == LearningMode::Self)
            {
                const LearningMove* existingMove = LD.probe_move(plm.key, plm.learningMove.move);
                if (existingMove)
                    plm.learningMove.score = existingMove->score;
                gameLine.push_back(plm);
            }
            else
            {
                LD.add_new_learning(plm.key, plm.learningMove);
            }
        }
        if (!enabledLearningProbe)
        {
            useLearning = false;
        }
    }
    // Kelly end

    // Send again PV info if we have a new best thread
    if (bestThread != this)
        sync_cout << UCI::pv(bestThread->rootPos, bestThread->completedDepth) << sync_endl;

    sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());

    if (bestThread->rootMoves[0].pv.size() > 1
        || bestThread->rootMoves[0].extract_ponder_from_tt(rootPos))
        std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

    std::cout << sync_endl;

    // from Khalid begin
    // Save learning data if game is already decided
    if (!bookMove)
    {
        if (Utility::is_game_decided(rootPos, (bestThread->rootMoves[0].score)) && LD.is_enabled()
            && !LD.is_paused())
        {
            // Perform Q-learning if enabled
            if (LD.learning_mode() == LearningMode::Self)
                putGameLineIntoLearningTable();
            // Save to learning file
            if (!LD.is_readonly())
            {
                LD.persist();
            }
            // Stop learning until we receive *ucinewgame* command
            LD.pause();
        }
    }

    // from Khalid end
// livebook begin
#ifdef USE_LIVEBOOK
    if (Options["Live Book"] && Options["Live Book Contribute"] && !g_inBook)
    {
        char*       szFen = curl_easy_escape(g_cURL, rootPos.fen().c_str(), 0);
        std::string szURL = g_livebookURL + "?action=store" + "&board=" + szFen + "&move=move:"
                          + UCI::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());
        curl_free(szFen);
        curl_easy_setopt(g_cURL, CURLOPT_URL, szURL.c_str());
        curl_easy_perform(g_cURL);
    }
#endif
    // livebook end
}


// Main iterative deepening loop. It calls search()
// repeatedly with increasing depth until the allocated thinking time has been
// consumed, the user stops the search, or the maximum search depth is reached.
void Thread::search() {

    // Allocate stack with extra size to allow access from (ss - 7) to (ss + 2):
    // (ss - 7) is needed for update_continuation_histories(ss - 1) which accesses (ss - 6),
    // (ss + 2) is needed for initialization of cutOffCnt and killers.
    Stack       stack[MAX_PLY + 10], *ss = stack + 7;
    Move        pv[MAX_PLY + 1];
    Value       alpha, beta, delta;
    Move        lastBestMove      = MOVE_NONE;
    Depth       lastBestMoveDepth = 0;
    MainThread* mainThread        = (this == Threads.main() ? Threads.main() : nullptr);
    double      timeReduction = 1, totBestMoveChanges = 0;
    Color       us      = rootPos.side_to_move();
    int         iterIdx = 0;

    std::memset(ss - 7, 0, 10 * sizeof(Stack));
    for (int i = 7; i > 0; --i)
    {
        (ss - i)->continuationHistory =
          &this->continuationHistory[0][0][NO_PIECE][0];  // Use as a sentinel
        (ss - i)->staticEval = VALUE_NONE;
    }

    for (int i = 0; i <= MAX_PLY + 2; ++i)
        (ss + i)->ply = i;

    ss->pv = pv;

    bestValue = -VALUE_INFINITE;

    if (mainThread)
    {
        if (mainThread->bestPreviousScore == VALUE_INFINITE)
            for (int i = 0; i < 4; ++i)
                mainThread->iterValue[i] = VALUE_ZERO;
        else
            for (int i = 0; i < 4; ++i)
                mainThread->iterValue[i] = mainThread->bestPreviousScore;
    }

    size_t multiPV = size_t(Options["MultiPV"]);
    Skill skill(Options["Skill Level"], Options["UCI_LimitStrength"] ? int(Options["UCI_Elo"]) : 0);

    // When playing with strength handicap enable MultiPV search that we will
    // use behind-the-scenes to retrieve a set of possible moves.
    if (skill.enabled())
        multiPV = std::max(multiPV, size_t(4));

    multiPV = std::min(multiPV, rootMoves.size());

    int searchAgainCounter = 0;
    // mcts begin
    bool mcts      = (bool) Options["MCTS"];
    bool possibleMCTSByValue    = false;
	isMCTS                      = false;
    if (mcts)
    {   
		bool maybeDraw = rootPos.rule50_count() >= 90 || rootPos.has_game_cycle(2);
        mctsThreads        = Options["MCTSThreads"];
        mctsGoldDigger     = Options["MCTSGoldDigger"];
        Value rootPosValue = (Value) (std::abs(int(static_value(rootPos, ss))));
        switch (mctsGoldDigger)
        {
        case 1 :
            possibleMCTSByValue = rootPosValue >= HIGH_MCTS;
            break;
        case 2 :
            possibleMCTSByValue = rootPosValue >= HIGH_MIDDLE_MCTS;
            break;
        case 3 :
            possibleMCTSByValue = rootPosValue >= MIDDLE_MCTS;
            break;
        case 4 :
            possibleMCTSByValue = rootPosValue >= MIDDLE_LOW_MCTS;
            break;
        case 5 :
            possibleMCTSByValue = rootPosValue >= LOW_MCTS;
            break;
        case 6 :
            possibleMCTSByValue = rootPosValue >= MIN_MCTS;
            break;
        default :
            break;
        }
		if ((!mainThread) && possibleMCTSByValue && (!maybeDraw) && (!Threads.stop))
		{    
			isMCTS                 = true;
			MonteCarlo* monteCarlo = new MonteCarlo(rootPos);
			if (!monteCarlo)
			{
				std::cerr << IO_LOCK << "Could not allocate " << sizeof(MonteCarlo)
						  << " bytes for MonteCarlo search" << std::endl
						  << IO_UNLOCK;
				::exit(EXIT_FAILURE);
			}

			monteCarlo->search();
			if (idx == 1 && Limits.infinite)
				monteCarlo->print_children();
			delete monteCarlo;
		}
    }
    if(!isMCTS)
    { 
    // from mcts end
    // Iterative deepening loop until requested to stop or the target depth is reached
    while (++rootDepth < MAX_PLY && !Threads.stop
           && !(Limits.depth && mainThread && rootDepth > Limits.depth))
    {
        // Age out PV variability metric
        if (mainThread)
            totBestMoveChanges /= 2;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : rootMoves)
            rm.previousScore = rm.score;

        size_t pvFirst = 0;
        pvLast         = 0;

        if (!Threads.increaseDepth)
            searchAgainCounter++;

        // MultiPV loop. We perform a full root search for each PV line
        for (pvIdx = 0; pvIdx < multiPV && !Threads.stop; ++pvIdx)
        {
            if (pvIdx == pvLast)
            {
                pvFirst = pvLast;
                for (pvLast++; pvLast < rootMoves.size(); pvLast++)
                    if (rootMoves[pvLast].tbRank != rootMoves[pvFirst].tbRank)
                        break;
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 0;

            // Reset aspiration window starting size
            Value avg = rootMoves[pvIdx].averageScore;
            delta     = Value(10) + int(avg) * avg / 17470;
            alpha     = std::max(avg - delta, -VALUE_INFINITE);
            beta      = std::min(avg + delta, VALUE_INFINITE);

            // Adjust optimism based on root move's averageScore (~4 Elo)
            int opt       = 113 * avg / (std::abs(avg) + 109);
            optimism[us]  = Value(opt);
            optimism[~us] = -optimism[us];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, re-search with a bigger window until we don't fail
            // high/low anymore.
            int failedHighCnt = 0;
            while (true)
            {
                // Adjust the effective depth searched, but ensure at least one effective increment
                // for every four searchAgain steps (see issue #2717).
                Depth adjustedDepth =
                  std::max(1, rootDepth - failedHighCnt - 3 * (searchAgainCounter + 1) / 4);
                bestValue = Brainlearn::search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and we want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.begin() + pvLast);

                // If search has been stopped, we break immediately. Sorting is
                // safe because RootMoves is still valid, although it refers to
                // the previous iteration.
                if (Threads.stop)
                    break;

                // When failing high/low give some update (without cluttering
                // the UI) before a re-search.
                if (mainThread && multiPV == 1 && (bestValue <= alpha || bestValue >= beta)
                    && Time.elapsed() > 3000)
                    sync_cout << UCI::pv(rootPos, rootDepth) << sync_endl;

                // In case of failing low/high increase aspiration window and
                // re-search, otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failedHighCnt = 0;
                    if (mainThread)
                        mainThread->stopOnPonderhit = false;
                }
                else if (bestValue >= beta)
                {
                    beta = std::min(bestValue + delta, VALUE_INFINITE);
                    ++failedHighCnt;
                }
                else
                    break;

                delta += delta / 3;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);

            if (mainThread && (Threads.stop || pvIdx + 1 == multiPV || Time.elapsed() > 3000))
                sync_cout << UCI::pv(rootPos, rootDepth) << sync_endl;
        }

        if (!Threads.stop)
            completedDepth = rootDepth;

        if (rootMoves[0].pv[0] != lastBestMove)
        {
            lastBestMove      = rootMoves[0].pv[0];
            lastBestMoveDepth = rootDepth;
        }

        // Have we found a "mate in x"?
        if (Limits.mate && bestValue >= VALUE_MATE_IN_MAX_PLY
            && VALUE_MATE - bestValue <= 2 * Limits.mate)
            Threads.stop = true;

        if (!mainThread)
            continue;

            // from true handicap mode begin
            /*
        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (skill.enabled() && skill.time_to_pick(rootDepth))
            skill.pick_best(multiPV);
      */
            // from true handicap mode end

        // Use part of the gained time from a previous stable move for the current move
        for (Thread* th : Threads)
        {
            totBestMoveChanges += th->bestMoveChanges;
            th->bestMoveChanges = 0;
        }

        // Do we have time for the next iteration? Can we stop searching now?
        if (Limits.use_time_management() && !Threads.stop && !mainThread->stopOnPonderhit)
        {
            double fallingEval = (66 + 14 * (mainThread->bestPreviousAverageScore - bestValue)
                                  + 6 * (mainThread->iterValue[iterIdx] - bestValue))
                               / 583.0;
            fallingEval = std::clamp(fallingEval, 0.5, 1.5);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 8 < completedDepth ? 1.56 : 0.69;
            double reduction = (1.4 + mainThread->previousTimeReduction) / (2.03 * timeReduction);
            double bestMoveInstability = 1 + 1.79 * totBestMoveChanges / Threads.size();

            double totalTime = Time.optimum() * fallingEval * reduction * bestMoveInstability;

            // Cap used time in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(500.0, totalTime);

            // Stop the search if we have exceeded the totalTime
            if (Time.elapsed() > totalTime)
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainThread->ponder)
                    mainThread->stopOnPonderhit = true;
                else
                    Threads.stop = true;
            }
            else if (!mainThread->ponder && Time.elapsed() > totalTime * 0.50)
                Threads.increaseDepth = false;
            else
                Threads.increaseDepth = true;
        }

        mainThread->iterValue[iterIdx] = bestValue;
        iterIdx                        = (iterIdx + 1) & 3;
        }
    }

    if (!mainThread)
        return;

    mainThread->previousTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (skill.enabled())
        std::swap(rootMoves[0], *std::find(rootMoves.begin(), rootMoves.end(),
                                           skill.best ? skill.best : skill.pick_best(multiPV)));
}


namespace {

// Main search function for both PV and non-PV nodes
template<NodeType nodeType>
Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch < PvNode ? PV : NonPV > (pos, ss, alpha, beta);

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (!rootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(pos.this_thread());
        if (alpha >= beta)
            return alpha;
    }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move      pv[MAX_PLY + 1], capturesSearched[32], quietsSearched[32];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, excludedMove = MOVE_NONE, bestMove, expTTMove = MOVE_NONE;  // from Kelly
    Depth    extension, newDepth;
    // from Kelly begin
    Value bestValue, value, ttValue, eval = VALUE_NONE, maxValue, expTTValue = VALUE_NONE,
                                     probCutBeta;
    bool givesCheck, improving, priorCapture, singularQuietLMR, expTTHit = false;
    //from Kelly End
    bool     capture, moveCountPruning, ttCapture;
    Piece    movedPiece;
    int      moveCount, captureCount, quietCount;
    // from Kelly begin
    bool updatedLearning = false;
    // flags to preserve node types
    bool disableNMAndPC = false;
    bool expectedPVNode = false;
    int  sibs           = 0;
    // from Kelly end
    // Step 1. Initialize node
    Thread* thisThread = pos.this_thread();
    ss->inCheck        = pos.checkers();
    priorCapture       = pos.captured_piece();
    Color us           = pos.side_to_move();
    moveCount = captureCount = quietCount = ss->moveCount = 0;
    bestValue                                             = -VALUE_INFINITE;
    maxValue                                              = VALUE_INFINITE;

    // Check for the available remaining time
    if (thisThread == Threads.main())
        static_cast<MainThread*>(thisThread)->check_time();

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (Threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos)
                                                        : value_draw(pos.this_thread());

        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply + 1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta)
            return alpha;
    }
    else
        thisThread->rootDelta = beta - alpha;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss + 1)->excludedMove = bestMove = MOVE_NONE;
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = MOVE_NONE;
    (ss + 2)->cutoffCnt                         = 0;
    ss->doubleExtensions                        = (ss - 1)->doubleExtensions;
    Square prevSq = is_ok((ss - 1)->currentMove) ? to_sq((ss - 1)->currentMove) : SQ_NONE;
    ss->statScore = 0;

    // Step 4. Transposition table lookup.
    excludedMove = ss->excludedMove;
    posKey       = pos.key();
    tte          = TT.probe(posKey, ss->ttHit);
    ttValue   = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove    = rootNode  ? thisThread->rootMoves[thisThread->pvIdx].pv[0]
              : ss->ttHit ? tte->move()
                          : MOVE_NONE;
    ttCapture = ttMove && pos.capture_stage(ttMove);

    // At this point, if excluded, skip straight to step 6, static eval. However,
    // to save indentation, we list the condition in all code between here and there.
    if (!excludedMove)
        ss->ttPv = PvNode || (ss->ttHit && tte->is_pv());

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && !excludedMove && tte->depth() > depth
        && ttValue != VALUE_NONE  // Possible in case of TT access race or if !ttHit
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~2 Elo)
                if (!ttCapture)
                    update_quiet_stats(pos, ss, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of
                // the previous ply (~0 Elo on STC, ~2 Elo on LTC).
                if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                    update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                                  -stat_bonus(depth + 1));
            }
            // Penalty for a quiet ttMove that fails low (~1 Elo)
            else if (!ttCapture)
            {
                int penalty = -stat_bonus(depth);
                thisThread->mainHistory[us][from_to(ttMove)] << penalty;
                update_continuation_histories(ss, pos.moved_piece(ttMove), to_sq(ttMove), penalty);
            }
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttValue;
    }
    // from Kelly begin
    // Step 4Bis. Global Learning Table lookup
    expTTHit        = false;
    updatedLearning = false;

    if (!excludedMove && LD.is_enabled() && useLearning)
    {
        const LearningMove* learningMove = nullptr;
        sibs                             = LD.probe(posKey, learningMove);
        if (learningMove)
        {
            assert(sibs);

            enabledLearningProbe = true;
            expTTHit             = true;
            if (!ttMove)
            {
                ttMove = learningMove->move;
            }

            if (learningMove->depth >= depth)
            {
                expTTMove       = learningMove->move;
                expTTValue      = learningMove->score;
                updatedLearning = true;
            }

            if ((learningMove->depth == 0))
                updatedLearning = false;

            if (updatedLearning && expTTValue != VALUE_NONE)


            {
                if (expTTValue < alpha)
                {
                    disableNMAndPC = true;
                }
                if (expTTValue > alpha && expTTValue < beta)
                {
                    expectedPVNode = true;
                    improving      = true;
                }
            }

            // At non-PV nodes we check for an early Global Learning Table cutoff
            // If expTTMove is quiet, update move sorting heuristics on global learning table hit
            if (!PvNode && updatedLearning
                && expTTValue
                     != VALUE_NONE  // Possible in case of Global Learning Table access race
                && (learningMove->depth >= depth))
            {
                if (expTTValue >= beta)
                {
                    if (!pos.capture_stage(learningMove->move))
                        update_quiet_stats(pos, ss, learningMove->move, stat_bonus(depth));

                    // Extra penalty for early quiet moves of the previous ply
                    if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                                      -stat_bonus(depth + 1));
                }
                // Penalty for a quiet ttMove that fails low
                else
                {
                    if (!pos.capture_stage(expTTMove))
                    {
                        int penalty = -stat_bonus(depth);
                        thisThread->mainHistory[us][from_to(expTTMove)] << penalty;
                        update_continuation_histories(ss, pos.moved_piece(expTTMove),
                                                      to_sq(expTTMove), penalty);
                    }
                }

                // thisThread->tbHits.fetch_add(1, std::memory_order_relaxed);
                if (pos.rule50_count() < 90)
                    return expTTValue;
            }
        }
    }
    // from Kelly end

    // Step 5. Tablebases probe
    if (!rootNode && !excludedMove && TB::Cardinality)
    {
        int piecesCount = pos.count<ALL_PIECES>();

        if (piecesCount <= TB::Cardinality
            && (piecesCount < TB::Cardinality || depth >= TB::ProbeDepth) && pos.rule50_count() == 0
            && !pos.can_castle(ANY_CASTLING))
        {
            TB::ProbeState err;
            TB::WDLScore   wdl = Tablebases::probe_wdl(pos, &err);

            // Force check of time on the next occasion
            if (thisThread == Threads.main())
                static_cast<MainThread*>(thisThread)->callsCnt = 0;

            if (err != TB::ProbeState::FAIL)
            {
                thisThread->tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = TB::UseRule50 ? 1 : 0;

                // use the range VALUE_MATE_IN_MAX_PLY to VALUE_TB_WIN_IN_MAX_PLY to score
                value = wdl < -drawScore ? VALUE_MATED_IN_MAX_PLY + ss->ply + 1
                      : wdl > drawScore  ? VALUE_MATE_IN_MAX_PLY - ss->ply - 1
                                         : VALUE_DRAW + 2 * wdl * drawScore;

                Bound b = wdl < -drawScore ? BOUND_UPPER
                        : wdl > drawScore  ? BOUND_LOWER
                                           : BOUND_EXACT;

                if (b == BOUND_EXACT || (b == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, b,
                              std::min(MAX_PLY - 1, depth + 6), MOVE_NONE, VALUE_NONE);

                    return value;
                }

                if (PvNode)
                {
                    if (b == BOUND_LOWER)
                        bestValue = value, alpha = std::max(alpha, bestValue);
                    else
                        maxValue = value;
                }
            }
        }
    }

    CapturePieceToHistory& captureHistory = thisThread->captureHistory;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving             = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often
        // brings significant Elo gain (~13 Elo).
        Eval::NNUE::hint_common_parent_position(pos);
        eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        ss->staticEval = eval = tte->eval();
        if (eval == VALUE_NONE)
            ss->staticEval = eval = evaluate(pos);
        else if (PvNode)
            Eval::NNUE::hint_common_parent_position(pos);

        // ttValue can be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        //Kelly begin
        if (!(expTTHit) || !(updatedLearning))
        {
            ss->staticEval = eval = evaluate(pos);

            // Save static evaluation into the transposition table
            tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, MOVE_NONE, eval);
        }
        else  // learning
        {
            // Never assume anything on values stored in Global Learning Table
            ss->staticEval = eval = expTTValue;
            if (eval == VALUE_NONE)
            {
                ss->staticEval = eval = evaluate(pos);
            }
            if (eval == VALUE_DRAW)
            {
                eval = value_draw(thisThread);
            }
            // Can expTTValue be used as a better position evaluation?
            if (expTTValue != VALUE_NONE)
            {
                eval = expTTValue;
            }
        }
    }
    // from Kelly end

    // Use static evaluation difference to improve quiet move ordering (~4 Elo)
    if (is_ok((ss - 1)->currentMove) && !(ss - 1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-18 * int((ss - 1)->staticEval + ss->staticEval), -1812, 1812);
        thisThread->mainHistory[~us][from_to((ss - 1)->currentMove)] << bonus;
    }

    // from Kelly begin
    if (!expectedPVNode)
    {
    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at our previous move we look at static evaluation at move prior to it
    // and if we were in check at move prior to it flag is set to true) and is
    // false otherwise. The improving flag is used in various pruning heuristics.
    improving = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
              : (ss - 4)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 4)->staticEval
                                                   : true;

    }
    // from Kelly end
    // Step 7. Razoring (~1 Elo)
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    // Adjust razor margin according to cutoffCnt. (~1 Elo)
    if (eval < alpha - 492 - (257 - 200 * ((ss + 1)->cutoffCnt > 3)) * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return value;
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 9
        && eval - futility_margin(depth, cutNode && !ss->ttHit, improving)
               - (ss - 1)->statScore / 321
             >= beta
        && eval >= beta && eval < 29462  // smaller than TB wins
        && !(!ttCapture && ttMove))
        return eval;

    // Step 9. Null move search with verification search (~35 Elo)
    if (!PvNode && !disableNMAndPC  //Kelly
        && (ss - 1)->currentMove != MOVE_NULL && (ss - 1)->statScore < 17257 && eval >= beta
        && eval >= ss->staticEval && ss->staticEval >= beta - 24 * depth + 281 && !excludedMove
        && pos.non_pawn_material(us) && ss->ply >= thisThread->nmpMinPly
        && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = std::min(int(eval - beta) / 152, 6) + depth / 3 + 4;

        ss->currentMove         = MOVE_NULL;
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (thisThread->nmpMinPly || depth < 14)
                return nullValue;

            assert(!thisThread->nmpMinPly);  // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning disabled
            // until ply exceeds nmpMinPly.
            thisThread->nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            thisThread->nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. If the position doesn't have a ttMove, decrease depth by 2,
    // or by 4 if the TT entry for the current position was hit and
    // the stored depth is greater than or equal to the current depth.
    // Use qsearch if depth is equal or below zero (~9 Elo)
    if (PvNode && !ttMove)
        depth -= 2 + 2 * (ss->ttHit && tte->depth() >= depth);

    if (depth <= 0)
        return qsearch<PV>(pos, ss, alpha, beta);

    if (cutNode && depth >= 8 && !ttMove)
        depth -= 2;

    probCutBeta = beta + 168 - 70 * improving;

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    if (
      !PvNode && !disableNMAndPC  // Kelly
      && depth > 3
      && abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If value from transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with transposition table cutoff depth is set to depth - 3
      // because probCut search has depth set to depth - 4 but we also do a move before it
      // So effective depth is equal to depth - 3
      && !(tte->depth() >= depth - 3 && ttValue != VALUE_NONE && ttValue < probCutBeta))
    {
        assert(probCutBeta < VALUE_INFINITE);

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &captureHistory);

        while ((move = mp.next_move()) != MOVE_NONE)
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture_stage(move));

                ss->currentMove = move;
                ss->continuationHistory =
                  &thisThread
                     ->continuationHistory[ss->inCheck][true][pos.moved_piece(move)][to_sq(move)];

                pos.do_move(move, st);

                // Perform a preliminary qsearch to verify that the move holds
                value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

                // If the qsearch held, perform the regular search
                if (value >= probCutBeta)
                    value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4,
                                           !cutNode);

                pos.undo_move(move);

                if (value >= probCutBeta)
                {
                    // Save ProbCut data into transposition table
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER, depth - 3,
                              move, ss->staticEval);
                    return value - (probCutBeta - beta);
                }
            }

        Eval::NNUE::hint_common_parent_position(pos);
    }

moves_loop:  // When in check, search starts here

    // Step 12. A small Probcut idea, when we are in check (~4 Elo)
    probCutBeta = beta + 416;
    if (ss->inCheck && !PvNode && ttCapture && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 4 && ttValue >= probCutBeta
        && abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    Move countermove =
      prevSq != SQ_NONE ? thisThread->counterMoves[pos.piece_on(prevSq)][prevSq] : MOVE_NONE;

    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &captureHistory, contHist,
                  countermove, ss->killers);

    value            = bestValue;
    moveCountPruning = singularQuietLMR = false;

    // Indicate PvNodes that will probably fail low if the node was searched
    // at a depth equal to or greater than the current depth, and the result
    // of this search was a fail low.
    bool likelyFailLow = PvNode && ttMove && (tte->bound() & BOUND_UPPER) && tte->depth() >= depth;

    // Step 13. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move(moveCountPruning)) != MOVE_NONE)
    {
        assert(is_ok(move));

        if (move == excludedMove)
            continue;

        // Check for legality
        if (!pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode
            && !std::count(thisThread->rootMoves.begin() + thisThread->pvIdx,
                           thisThread->rootMoves.begin() + thisThread->pvLast, move))
            continue;

        ss->moveCount = ++moveCount;

        if (rootNode && thisThread == Threads.main() && Time.elapsed() > 3000)
            sync_cout << "info depth " << depth << " currmove "
                      << UCI::move(move, pos.is_chess960()) << " currmovenumber "
                      << moveCount + thisThread->pvIdx << sync_endl;
        if (PvNode)
            (ss + 1)->pv = nullptr;

        extension  = 0;
        capture    = pos.capture_stage(move);
        movedPiece = pos.moved_piece(move);
        givesCheck = pos.gives_check(move);

        // Calculate new depth for this move
        newDepth = depth - 1;

        Value delta = beta - alpha;

        Depth r = reduction(improving, depth, moveCount, delta, thisThread->rootDelta);

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!rootNode && pos.non_pawn_material(us) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~8 Elo)
            if (!moveCountPruning)
                moveCountPruning = moveCount >= futility_move_count(improving, depth);

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Piece capturedPiece = pos.piece_on(to_sq(move));
                    int   futilityEval =
                      ss->staticEval + 239 + 291 * lmrDepth + PieceValue[capturedPiece]
                      + captureHistory[movedPiece][to_sq(move)][type_of(capturedPiece)] / 7;
                    if (futilityEval < alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                if (!pos.see_ge(move, Value(-185) * depth))
                    continue;
            }
            else
            {
                int history = (*contHist[0])[movedPiece][to_sq(move)]
                            + (*contHist[1])[movedPiece][to_sq(move)]
                            + (*contHist[3])[movedPiece][to_sq(move)];

                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 6 && history < -3498 * depth)
                    continue;

                history += 2 * thisThread->mainHistory[us][from_to(move)];

                lmrDepth += history / 7815;
                lmrDepth = std::max(lmrDepth, -2);

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 13 && ss->staticEval + 80 + 122 * lmrDepth <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, Value(-27 * lmrDepth * lmrDepth)))
                    continue;
            }
        }

        // Step 15. Extensions (~100 Elo)
        // We take care to not overdo to avoid search getting stuck.
        if (ss->ply < thisThread->rootDepth * 2)
        {
            // Singular extension search (~94 Elo). If all moves but one fail low on a
            // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
            // then that move is singular and should be extended. To verify this we do
            // a reduced search on all the other moves but the ttMove and if the result
            // is lower than ttValue minus a margin, then we will extend the ttMove. Note
            // that depth margin and singularBeta margin are known for having non-linear
            // scaling. Their values are optimized to time controls of 180+1.8 and longer
            // so changing them requires tests at this type of time controls.
            // Recursive singular search is avoided.
            if (!rootNode && move == ttMove && !excludedMove
                && depth >= 4 - (thisThread->completedDepth > 24) + 2 * (PvNode && tte->is_pv())
                && abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (tte->bound() & BOUND_LOWER)
                && tte->depth() >= depth - 3)
            {
                Value singularBeta  = ttValue - (64 + 57 * (ss->ttPv && !PvNode)) * depth / 64;
                Depth singularDepth = (depth - 1) / 2;

                ss->excludedMove = move;
                value =
                  search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = MOVE_NONE;

                if (value < singularBeta)
                {
                    extension        = 1;
                    singularQuietLMR = !ttCapture;

                    // Avoid search explosion by limiting the number of double extensions
                    if (!PvNode && value < singularBeta - 18 && ss->doubleExtensions <= 11)
                    {
                        extension = 2;
                        depth += depth < 15;
                    }
                }

                // Multi-cut pruning
                // Our ttMove is assumed to fail high, and now we failed high also on a
                // reduced search without the ttMove. So we assume this expected cut-node
                // is not singular, that multiple moves fail high, and we can prune the
                // whole subtree by returning a softbound.
                else if (singularBeta >= beta)
                    return singularBeta;

                // If the eval of ttMove is greater than beta, reduce it (negative extension) (~7 Elo)
                else if (ttValue >= beta)
                    extension = -2 - !PvNode;

                // If we are on a cutNode, reduce it based on depth (negative extension) (~1 Elo)
                else if (cutNode)
                    extension = depth < 19 ? -2 : -1;

                // If the eval of ttMove is less than value, reduce it (negative extension) (~1 Elo)
                else if (ttValue <= value)
                    extension = -1;
            }

            // Check extensions (~1 Elo)
            else if (givesCheck && depth > 9)
                extension = 1;

            // Quiet ttMove extensions (~1 Elo)
            else if (PvNode && move == ttMove && move == ss->killers[0]
                     && (*contHist[0])[movedPiece][to_sq(move)] >= 4194)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;
        ss->doubleExtensions = (ss - 1)->doubleExtensions + (extension == 2);

        // Speculative prefetch as early as possible
        prefetch(TT.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread->continuationHistory[ss->inCheck][capture][movedPiece][to_sq(move)];

        // Step 16. Make the move
        pos.do_move(move, st, givesCheck);

        // Decrease reduction if position is or has been on the PV (~4 Elo)
        if (ss->ttPv && !likelyFailLow)
            r -= cutNode && tte->depth() >= depth ? 3 : 2;

        // Decrease reduction if opponent's move count is high (~1 Elo)
        if ((ss - 1)->moveCount > 7)
            r--;

        // Increase reduction for cut nodes (~3 Elo)
        if (cutNode)
            r += 2;

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Decrease reduction for PvNodes (~2 Elo)
        if (PvNode)
            r--;

        // Decrease reduction if ttMove has been singularly extended (~1 Elo)
        if (singularQuietLMR)
            r--;

        // Increase reduction on repetition (~1 Elo)
        if (move == (ss - 4)->currentMove && pos.has_repeated())
            r += 2;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss + 1)->cutoffCnt > 3)
            r++;

        // Decrease reduction for first generated move (ttMove)
        else if (move == ttMove)
            r--;

        ss->statScore = 2 * thisThread->mainHistory[us][from_to(move)]
                      + (*contHist[0])[movedPiece][to_sq(move)]
                      + (*contHist[1])[movedPiece][to_sq(move)]
                      + (*contHist[3])[movedPiece][to_sq(move)] - 3848;

        // Decrease/increase reduction for moves with a good/bad history (~25 Elo)
        r -= ss->statScore / (10216 + 3855 * (depth > 5 && depth < 23));

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        // We use various heuristics for the sons of a node after the first son has
        // been searched. In general, we would like to reduce them, but there are many
        // cases where we extend a son if it has good chances to be "interesting".
        if (depth >= 2 && moveCount > sibs  //Kelly
            && moveCount > 1 + (PvNode && ss->ply <= 1)
            && (!ss->ttPv || !capture || (cutNode && (ss - 1)->moveCount > 1)))
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move depth. This may lead to hidden double extensions.
            Depth d = std::clamp(newDepth - r, 1, newDepth + 1);

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch     = value > (bestValue + 51 + 10 * (newDepth - d));
                const bool doEvenDeeperSearch = value > alpha + 700 && ss->doubleExtensions <= 6;
                const bool doShallowerSearch  = value < bestValue + newDepth;

                ss->doubleExtensions = ss->doubleExtensions + doEvenDeeperSearch;

                newDepth += doDeeperSearch - doShallowerSearch + doEvenDeeperSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                int bonus = value <= alpha ? -stat_bonus(newDepth)
                          : value >= beta  ? stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, movedPiece, to_sq(move), bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction for cut nodes and not ttMove (~1 Elo)
            if (!ttMove && cutNode)
                r += 2;

            // Note that if expected reduction is high, we reduce search depth by 1 here
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - (r > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PvNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv    = pv;
            (ss + 1)->pv[0] = MOVE_NONE;

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        // Step 19. Undo move
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (Threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm =
              *std::find(thisThread->rootMoves.begin(), thisThread->rootMoves.end(), move);

            rm.averageScore =
              rm.averageScore != -VALUE_INFINITE ? (2 * value + rm.averageScore) / 3 : value;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.score = rm.uciScore = value;
                rm.selDepth            = thisThread->selDepth;
                rm.scoreLowerbound = rm.scoreUpperbound = false;

                if (value >= beta)
                {
                    rm.scoreLowerbound = true;
                    rm.uciScore        = beta;
                }
                else if (value <= alpha)
                {
                    rm.scoreUpperbound = true;
                    rm.uciScore        = alpha;
                }

                rm.pv.resize(1);

                assert((ss + 1)->pv);

                for (Move* m = (ss + 1)->pv; *m != MOVE_NONE; ++m)
                    rm.pv.push_back(*m);

                // We record how often the best move has been changed in each iteration.
                // This information is used for time management. In MultiPV mode,
                // we must take care to only do this for the first PV line.
                if (moveCount > 1 && !thisThread->pvIdx)
                    ++thisThread->bestMoveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value: this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.score = -VALUE_INFINITE;
        }

        if (value > bestValue)
        {
            bestValue = value;

            if (value > alpha)
            {
                bestMove = move;

                if (PvNode && !rootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    ss->cutoffCnt += 1 + !ttMove;
                    assert(value >= beta);  // Fail high
                    break;
                }
                else
                {
                    // Reduce other moves if we have found at least one score improvement (~2 Elo)
                    if (depth > 2 && depth < 12 && beta < 13828 && value > -11369)
                        depth -= 2;

                    assert(depth > 0);
                    alpha = value;  // Update alpha! Always alpha < beta
                }
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, to update its stats later.
        if (move != bestMove && moveCount <= 32)
        {
            if (capture)
                capturesSearched[captureCount++] = move;

            else
                quietsSearched[quietCount++] = move;
        }
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !ss->inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha we update the stats of searched moves
    else if (bestMove)
        update_all_stats(pos, ss, bestMove, bestValue, beta, prevSq, quietsSearched, quietCount,
                         capturesSearched, captureCount, depth);

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (depth > 6) + (PvNode || cutNode) + (bestValue < alpha - 653)
                  + ((ss - 1)->moveCount > 11);
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      stat_bonus(depth) * bonus);
        thisThread->mainHistory[~us][from_to((ss - 1)->currentMove)]
          << stat_bonus(depth) * bonus / 2;
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta    ? BOUND_LOWER
                  : PvNode && bestMove ? BOUND_EXACT
                                       : BOUND_UPPER,
                  depth, bestMove, ss->staticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}


// Quiescence search function, which is called by the main search
// function with zero depth, or recursively with further decreasing depth per call.
// (~155 Elo)
template<NodeType nodeType>
Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= 0);

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(pos.this_thread());
        if (alpha >= beta)
            return alpha;
    }

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, bestMove;
    Depth    ttDepth;
    Value    bestValue, value, ttValue, futilityValue, futilityBase;
    bool     pvHit, givesCheck, capture;
    int      moveCount;
    Color    us = pos.side_to_move();

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss + 1)->pv = pv;
        ss->pv[0]    = MOVE_NONE;
    }

    Thread* thisThread = pos.this_thread();
    bestMove           = MOVE_NONE;
    ss->inCheck        = pos.checkers();
    moveCount          = 0;

    // Step 2. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide whether or not to include checks: this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    ttDepth = ss->inCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

    // Step 3. Transposition table lookup
    posKey  = pos.key();
    tte     = TT.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove  = ss->ttHit ? tte->move() : MOVE_NONE;
    pvHit   = ss->ttHit && tte->is_pv();

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && tte->depth() >= ttDepth
        && ttValue != VALUE_NONE  // Only in case of TT access race or if !ttHit
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
        bestValue = futilityBase = -VALUE_INFINITE;
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            if ((ss->staticEval = bestValue = tte->eval()) == VALUE_NONE)
                ss->staticEval = bestValue = evaluate(pos);

            // ttValue can be used as a better position evaluation (~13 Elo)
            if (ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
            // In case of null move search use previous static eval with a different sign
            ss->staticEval = bestValue =
              (ss - 1)->currentMove != MOVE_NULL ? evaluate(pos) : -(ss - 1)->staticEval;

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER, DEPTH_NONE,
                          MOVE_NONE, ss->staticEval);

            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 200;
    }

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory};

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS_CHECKS)
    // will be generated.
    Square     prevSq = is_ok((ss - 1)->currentMove) ? to_sq((ss - 1)->currentMove) : SQ_NONE;
    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &thisThread->captureHistory,
                  contHist, prevSq);

    int quietCheckEvasions = 0;

    // Step 5. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move()) != MOVE_NONE)
    {
        assert(is_ok(move));

        // Check for legality
        if (!pos.legal(move))
            continue;

        givesCheck = pos.gives_check(move);
        capture    = pos.capture_stage(move);

        moveCount++;

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(us))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && to_sq(move) != prevSq && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && type_of(move) != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                futilityValue = futilityBase + PieceValue[pos.piece_on(to_sq(move))];

                // If static eval + value of piece we are going to capture is much lower
                // than alpha we can prune this move.
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // If static eval is much lower than alpha and move is not winning material
                // we can prune this move.
                if (futilityBase <= alpha && !pos.see_ge(move, VALUE_ZERO + 1))
                {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }

                // If static exchange evaluation is much worse than what is needed to not
                // fall below alpha we can prune this move.
                if (futilityBase > alpha && !pos.see_ge(move, (alpha - futilityBase) * 4))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // We prune after the second quiet check evasion move, where being 'in check' is
            // implicitly checked through the counter, and being a 'quiet move' apart from
            // being a tt move is assumed after an increment because captures are pushed ahead.
            if (quietCheckEvasions > 1)
                break;

            // Continuation history based pruning (~3 Elo)
            if (!capture && (*contHist[0])[pos.moved_piece(move)][to_sq(move)] < 0
                && (*contHist[1])[pos.moved_piece(move)][to_sq(move)] < 0)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, Value(-90)))
                continue;
        }

        // Speculative prefetch as early as possible
        prefetch(TT.first_entry(pos.key_after(move)));

        // Update the current move
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread
             ->continuationHistory[ss->inCheck][capture][pos.moved_piece(move)][to_sq(move)];

        quietCheckEvasions += !capture && ss->inCheck;

        // Step 7. Make and search the move
        pos.do_move(move, st, givesCheck);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha, depth - 1);
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 8. Check for a new best move
        if (value > bestValue)
        {
            bestValue = value;

            if (value > alpha)
            {
                bestMove = move;

                if (PvNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value < beta)  // Update alpha here!
                    alpha = value;
                else
                    break;  // Fail high
            }
        }
    }
    // from Sugar
    if (openingVariety && bestValue + (openingVariety * UCI::NormalizeToPawnValue / 100) >= 0
        && pos.count<PAWN>() > 12)
        bestValue += thisThread->nodes % (openingVariety + 1);
    // end from Sugar
    // Step 9. Check for mate
    // All legal moves have been searched. A special case: if we're in check
    // and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(!MoveList<LEGAL>(pos).size());

        return mated_in(ss->ply);  // Plies to mate from the root
    }

    // Save gathered info in transposition table
    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit,
              bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, ttDepth, bestMove, ss->staticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}


// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v + ply : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}


// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate scores related to the 50 moves rule
// and the graph history interaction problem, we return an optimal TB score instead.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    if (v >= VALUE_TB_WIN_IN_MAX_PLY)  // TB win or better
    {
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 99 - r50c)
            return VALUE_MATE_IN_MAX_PLY - 1;  // do not return a potentially false mate score

        return v - ply;
    }

    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)  // TB loss or worse
    {
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 99 - r50c)
            return VALUE_MATED_IN_MAX_PLY + 1;  // do not return a potentially false mate score

        return v + ply;
    }

    return v;
}


// Adds current move and appends child pv[]
void update_pv(Move* pv, Move move, const Move* childPv) {

    for (*pv++ = move; childPv && *childPv != MOVE_NONE;)
        *pv++ = *childPv++;
    *pv = MOVE_NONE;
}


// Updates stats at the end of search() when a bestMove is found
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Move            bestMove,
                      Value           bestValue,
                      Value           beta,
                      Square          prevSq,
                      Move*           quietsSearched,
                      int             quietCount,
                      Move*           capturesSearched,
                      int             captureCount,
                      Depth           depth) {

    Color                  us             = pos.side_to_move();
    Thread*                thisThread     = pos.this_thread();
    CapturePieceToHistory& captureHistory = thisThread->captureHistory;
    Piece                  moved_piece    = pos.moved_piece(bestMove);
    PieceType              captured;

    int quietMoveBonus = stat_bonus(depth + 1);

    if (!pos.capture_stage(bestMove))
    {
        int bestMoveBonus = bestValue > beta + 168 ? quietMoveBonus      // larger bonus
                                                   : stat_bonus(depth);  // smaller bonus

        // Increase stats for the best move in case it was a quiet move
        update_quiet_stats(pos, ss, bestMove, bestMoveBonus);

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietCount; ++i)
        {
            thisThread->mainHistory[us][from_to(quietsSearched[i])] << -bestMoveBonus;
            update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]),
                                          to_sq(quietsSearched[i]), -bestMoveBonus);
        }
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        captured = type_of(pos.piece_on(to_sq(bestMove)));
        captureHistory[moved_piece][to_sq(bestMove)][captured] << quietMoveBonus;
    }

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer move in previous ply when it gets refuted.
    if (prevSq != SQ_NONE
        && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit
            || ((ss - 1)->currentMove == (ss - 1)->killers[0]))
        && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, -quietMoveBonus);

    // Decrease stats for all non-best capture moves
    for (int i = 0; i < captureCount; ++i)
    {
        moved_piece = pos.moved_piece(capturesSearched[i]);
        captured    = type_of(pos.piece_on(to_sq(capturesSearched[i])));
        captureHistory[moved_piece][to_sq(capturesSearched[i])][captured] << -quietMoveBonus;
    }
}


// Updates histories of the move pairs formed
// by moves at ply -1, -2, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (is_ok((ss - i)->currentMove))
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 3));
    }
}


// Updates move sorting heuristics
void update_quiet_stats(const Position& pos, Stack* ss, Move move, int bonus) {

    // Update killers
    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color   us         = pos.side_to_move();
    Thread* thisThread = pos.this_thread();
    thisThread->mainHistory[us][from_to(move)] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), to_sq(move), bonus);

    // Update countermove history
    if (is_ok((ss - 1)->currentMove))
    {
        Square prevSq                                          = to_sq((ss - 1)->currentMove);
        thisThread->counterMoves[pos.piece_on(prevSq)][prevSq] = move;
    }
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_best(size_t multiPV) {

    const RootMoves& rootMoves = Threads.main()->rootMoves;
    static PRNG      rng(now());  // PRNG sequence should be non-deterministic

    // RootMoves are already sorted by score in descending order
    Value  topScore = rootMoves[0].score;
    int    delta    = std::min(topScore - rootMoves[multiPV - 1].score, PawnValue);
    int    maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    // Choose best move. For each move score we add two terms, both dependent on
    // weakness. One is deterministic and bigger for weaker levels, and one is
    // random. Then we choose the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = int((weakness * int(topScore - rootMoves[i].score)
                        + delta * (rng.rand<unsigned>() % int(weakness)))
                       / 128);

        if (rootMoves[i].score + push >= maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best     = rootMoves[i].pv[0];
        }
    }

    return best;
}

}  // namespace

// mcts begin
//  minimax_value() is a wrapper around the search() and qsearch() functions
//  used to compute the minimax evaluation of a position at the given depth,
//  from the point of view of the side to move. It does not compute PV nor
//  emit anything on the output stream. Note: you can call this function
//  with depth == DEPTH_ZERO to compute the quiescence value of the position.

Value minimax_value(Position& pos, Search::Stack* ss, Depth depth) {

    //    Threads.stopOnPonderhit = Threads.stop = false;
    Value alpha = -VALUE_INFINITE;
    Value beta  = VALUE_INFINITE;
    Move  pv[MAX_PLY + 1];
    ss->pv = pv;

    /*   if (pos.should_debug())
      {
          debug << "Entering minimax_value() for the following position:" << std::endl;
          debug << pos << std::endl;
          hit_any_key();
      }*/

    Value value = search<PV>(pos, ss, alpha, beta, depth, false);

    // Have we found a "mate in x"?
    if (Limits.mate && value >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - value <= 2 * Limits.mate)
        Threads.stop = true;

    /*    if (pos.should_debug())
      {
          debug << pos << std::endl;
          debug << "... exiting minimax_value() with value = " << value << std::endl;
          hit_any_key();
      }
    */
    return value;
}

// minimax_value() is a wrapper around the search() and qsearch() functions
// used to compute the minimax evaluation of a position at the given depth,
// from the point of view of the side to move. It does not compute PV nor
// emit anything on the output stream. Note: you can call this function
// with depth == DEPTH_ZERO to compute the quiescence value of the position.

Value minimax_value(Position& pos, Search::Stack* ss, Depth depth, Value alpha, Value beta) {

    //    Threads.stopOnPonderhit = Threads.stop = false;
    //   alpha = -VALUE_INFINITE;
    //   beta = VALUE_INFINITE;
    Move pv[MAX_PLY + 1];
    ss->pv = pv;

    /*   if (pos.should_debug())
      {
          debug << "Entering minimax_value() for the following position:" << std::endl;
          debug << pos << std::endl;
          hit_any_key();
      }*/
    Value value = VALUE_ZERO;
    Value delta = Value(18);

    while (!Threads.stop.load(std::memory_order_relaxed))
    {
        value = search<PV>(pos, ss, alpha, beta, depth, false);
        if (value <= alpha)
        {
            beta  = (alpha + beta) / 2;
            alpha = std::max(value - delta, -VALUE_INFINITE);
        }
        else if (value >= beta)
        {
            beta = std::min(value + delta, VALUE_INFINITE);
            // ++failedHighCnt;
        }
        else
        {
            //++rootMoves[pvIdx].bestMoveCount;
            break;
        }
    }

    // Have we found a "mate in x"?
    if (Limits.mate && value >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - value <= 2 * Limits.mate)
        Threads.stop = true;

    /*    if (pos.should_debug())
      {
          debug << pos << std::endl;
          debug << "... exiting minimax_value() with value = " << value << std::endl;
          hit_any_key();
      }
    */
    return value;
}
// mcts end

/// MainThread::check_time() is used to print debug info and, more importantly,
/// to detect when we are out of available time and thus stop the search.

// Used to print debug info and, more importantly,
// to detect when we are out of available time and thus stop the search.
void MainThread::check_time() {

    if (--callsCnt > 0)
        return;

    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCnt = Limits.nodes ? std::min(512, int(Limits.nodes / 1024)) : 512;

    static TimePoint lastInfoTime = now();

    TimePoint elapsed = Time.elapsed();
    TimePoint tick    = Limits.startTime + elapsed;

    if (tick - lastInfoTime >= 1000)
    {
        lastInfoTime = tick;
        dbg_print();
    }

    // We should not stop pondering until told so by the GUI
    if (ponder)
        return;

    if ((Limits.use_time_management() && (elapsed > Time.maximum() || stopOnPonderhit))
        || (Limits.movetime && elapsed >= Limits.movetime)
        || (Limits.nodes && Threads.nodes_searched() >= uint64_t(Limits.nodes)))
        Threads.stop = true;
}


// Formats PV information according to the UCI protocol. UCI requires
// that all (if any) unsearched PV lines are sent using a previous search score.
string UCI::pv(const Position& pos, Depth depth) {

    std::stringstream ss;
    TimePoint         elapsed       = Time.elapsed() + 1;
    const RootMoves&  rootMoves     = pos.this_thread()->rootMoves;
    size_t            pvIdx         = pos.this_thread()->pvIdx;
    size_t            multiPV       = std::min(size_t(Options["MultiPV"]), rootMoves.size());
    uint64_t          nodesSearched = Threads.nodes_searched();
    uint64_t          tbHits        = Threads.tb_hits() + (TB::RootInTB ? rootMoves.size() : 0);

    for (size_t i = 0; i < multiPV; ++i)
    {
        bool updated = rootMoves[i].score != -VALUE_INFINITE;

        if (depth == 1 && !updated && i > 0)
            continue;

        Depth d = updated ? depth : std::max(1, depth - 1);
        Value v = updated ? rootMoves[i].uciScore : rootMoves[i].previousScore;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = TB::RootInTB && abs(v) < VALUE_MATE_IN_MAX_PLY;
        v       = tb ? rootMoves[i].tbScore : v;

        if (ss.rdbuf()->in_avail())  // Not at first line
            ss << "\n";

        ss << "info"
           << " depth " << d << " seldepth " << rootMoves[i].selDepth << " multipv " << i + 1
           << " score " << UCI::value(v);

        if (Options["UCI_ShowWDL"])
            ss << UCI::wdl(v, pos.game_ply());

        if (i == pvIdx && !tb && updated)  // tablebase- and previous-scores are exact
            ss << (rootMoves[i].scoreLowerbound
                     ? " lowerbound"
                     : (rootMoves[i].scoreUpperbound ? " upperbound" : ""));

        ss << " nodes " << nodesSearched << " nps " << nodesSearched * 1000 / elapsed
           << " hashfull " << TT.hashfull() << " tbhits " << tbHits << " time " << elapsed << " pv";

        for (Move m : rootMoves[i].pv)
            ss << " " << UCI::move(m, pos.is_chess960());
    }

    return ss.str();
}


// Called in case we have no ponder move
// before exiting the search, for instance, in case we stop the search during a
// fail high at root. We try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' we have nothing to think about.
bool RootMove::extract_ponder_from_tt(Position& pos) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    bool ttHit;

    assert(pv.size() == 1);

    if (pv[0] == MOVE_NONE)
        return false;

    pos.do_move(pv[0], st);
    TTEntry* tte = TT.probe(pos.key(), ttHit);

    if (ttHit)
    {
        Move m = tte->move();  // Local copy to be SMP safe
        if (MoveList<LEGAL>(pos).contains(m))
            pv.push_back(m);
    }

    pos.undo_move(pv[0]);
    return pv.size() > 1;
}

void Tablebases::rank_root_moves(Position& pos, Search::RootMoves& rootMoves) {

    RootInTB           = false;
    UseRule50          = bool(Options["Syzygy50MoveRule"]);
    ProbeDepth         = int(Options["SyzygyProbeDepth"]);
    Cardinality        = int(Options["SyzygyProbeLimit"]);
    bool dtz_available = true;

    // Tables with fewer pieces than SyzygyProbeLimit are searched with
    // ProbeDepth == DEPTH_ZERO
    if (Cardinality > MaxCardinality)
    {
        Cardinality = MaxCardinality;
        ProbeDepth  = 0;
    }

    if (Cardinality >= popcount(pos.pieces()) && !pos.can_castle(ANY_CASTLING))
    {
        // Rank moves using DTZ tables
        RootInTB = root_probe(pos, rootMoves);

        if (!RootInTB)
        {
            // DTZ tables are missing; try to rank moves using WDL tables
            dtz_available = false;
            RootInTB      = root_probe_wdl(pos, rootMoves);
        }
    }

    if (RootInTB)
    {
        // Sort moves according to TB rank
        std::stable_sort(rootMoves.begin(), rootMoves.end(),
                         [](const RootMove& a, const RootMove& b) { return a.tbRank > b.tbRank; });

        // Probe during search only if DTZ is not available and we are winning
        if (dtz_available || rootMoves[0].tbScore <= VALUE_DRAW)
            Cardinality = 0;
    }
    else
    {
        // Clean up if root_probe() and root_probe_wdl() have failed
        for (auto& m : rootMoves)
            m.tbRank = 0;
    }
}

// Kelly begin
void putGameLineIntoLearningTable() {
    double learning_rate = 0.5;
    double gamma         = 0.99;

    if (gameLine.size() > 1)
    {
        for (size_t index = gameLine.size() - 1; index > 0; index--)
        {
            int currentScore =
              gameLine[index - 1].learningMove.score * 100 / UCI::NormalizeToPawnValue;
            int nextScore = gameLine[index].learningMove.score * 100 / UCI::NormalizeToPawnValue;

            currentScore = currentScore * (1 - learning_rate) + learning_rate * (gamma * nextScore);

            gameLine[index - 1].learningMove.score =
              currentScore * (Value) (UCI::NormalizeToPawnValue) / 100;

            LD.add_new_learning(gameLine[index - 1].key, gameLine[index - 1].learningMove);
        }

        gameLine.clear();
    }
}

void setStartPoint() {
    useLearning = true;
    LD.resume();
}
// Kelly end

}  // namespace Brainlearn
