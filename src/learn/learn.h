#ifndef LEARN_H_INCLUDED
#define LEARN_H_INCLUDED

#include <unordered_map>
#include "../types.h"
#include "../ucioption.h"

enum class LearningMode {
    Off      = 1,
    Standard = 2,
    Self     = 3,
};

struct LearningMove {
    Brainlearn::Depth depth       = 0;
    Brainlearn::Value score       = Brainlearn::VALUE_NONE;
    Brainlearn::Move  move        = Brainlearn::Move::none();
    int               performance = 100;
};

struct PersistedLearningMove {
    Brainlearn::Key key;
    LearningMove    learningMove;
};

class LearningData {
   private:
    bool         isPaused;
    bool         isReadOnly;
    bool         needPersisting;
    LearningMode learningMode;

    std::unordered_multimap<Brainlearn::Key, LearningMove*> HT;
    std::vector<void*>                                      mainDataBuffers;
    std::vector<void*>                                      newMovesDataBuffers;

   private:
    bool load(const std::string& filename);
    void insert_or_update(PersistedLearningMove* plm, bool qLearning);

   public:
    LearningData();
    ~LearningData();

    void        pause();
    void        resume();
    inline bool is_paused() const { return isPaused; };

    void         set_learning_mode(Brainlearn::OptionsMap options, const std::string& lm);
    LearningMode learning_mode() const;
    inline bool  is_enabled() const { return learningMode != LearningMode::Off; }

    void        set_readonly(bool ro) { isReadOnly = ro; }
    inline bool is_readonly() const { return isReadOnly; }

    void clear();
    void init(Brainlearn::OptionsMap& o);
    void persist(const Brainlearn::OptionsMap& o);

    void add_new_learning(Brainlearn::Key key, const LearningMove& lm);

    int probeByMaxDepthAndScore(Brainlearn::Key key, const LearningMove*& learningMove);
    const LearningMove* probe_move(Brainlearn::Key key, Brainlearn::Move move);
};

extern LearningData LD;

#endif  // #ifndef LEARN_H_INCLUDED
