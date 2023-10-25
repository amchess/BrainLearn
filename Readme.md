<div align="center">

  [![Stockfish][stockfish128-logo]][website-link]

  <h3>Stockfish</h3>

  A free and strong UCI chess engine.
  <br>
  <strong>[Explore Stockfish docs »][wiki-link]</strong>
  <br>
  <br>
  [Report bug][issue-link]
  ·
  [Open a discussion][discussions-link]
  ·
  [Discord][discord-link]
  ·
  [Blog][website-blog-link]

  [![Build][build-badge]][build-link]
  [![License][license-badge]][license-link]
  <br>
  [![Release][release-badge]][release-link]
  [![Commits][commits-badge]][commits-link]
  <br>
  [![Website][website-badge]][website-link]
  [![Fishtest][fishtest-badge]][fishtest-link]
  [![Discord][discord-badge]][discord-link]

</div>

## Overview

[Stockfish][website-link] is a **free and strong UCI chess engine** derived from
Glaurung 2.1 that analyzes chess positions and computes the optimal moves.

Stockfish **does not include a graphical user interface** (GUI) that is required
to display a chessboard and to make it easy to input moves. These GUIs are
developed independently from Stockfish and are available online. **Read the
documentation for your GUI** of choice for information about how to use
Stockfish with it.

See also the Stockfish [documentation][wiki-usage-link] for further usage help.

## Files

This distribution of Stockfish consists of the following files:

  * [README.md][readme-link], the file you are currently reading.

  * [Copying.txt][license-link], a text file containing the GNU General Public
    License version 3.

  * [AUTHORS][authors-link], a text file with the list of authors for the project.

  * [src][src-link], a subdirectory containing the full source code, including a
    Makefile that can be used to compile Stockfish on Unix-like systems.

  * a file with the .nnue extension, storing the neural network for the NNUE
    evaluation. Binary distributions will have this file embedded.

## Contributing

__See [Contributing Guide](CONTRIBUTING.md).__

### Donating hardware

Improving Stockfish requires a massive amount of testing. You can donate your
hardware resources by installing the [Fishtest Worker][worker-link] and viewing
the current tests on [Fishtest][fishtest-link].

### Improving the code

In the [chessprogramming wiki][programming-link], many techniques used in
Stockfish are explained with a lot of background information.
The [section on Stockfish][programmingsf-link] describes many features
and techniques used by Stockfish. However, it is generic rather than
focused on Stockfish's precise implementation.

The engine testing is done on [Fishtest][fishtest-link].
If you want to help improve Stockfish, please read this [guideline][guideline-link]
first, where the basics of Stockfish development are explained.

Discussions about Stockfish take place these days mainly in the Stockfish
[Discord server][discord-link]. This is also the best place to ask questions
about the codebase and how to improve it.

## The UCI protocol

The [Universal Chess Interface][uci-link] (UCI) is a standard text-based protocol
used to communicate with a chess engine and is the recommended way to do so for
typical graphical user interfaces (GUI) or chess tools. Brainlearn implements the
majority of its options.

Developers can see the default values for the UCI options available in Brainlearn
by typing `./brainlearn uci` in a terminal, but most users should typically use a
chess GUI to interact with Brainlearn.

For more information on UCI or debug commands, see our [documentation][wiki-commands-link].


## UCI parameters


BrainLearn hash the same options as BrainFish, but it implements a persisted learning algorithm, managing a file named experience.exp.

It is a collection of one or more positions stored with the following format (similar to in memory Brainlearn Transposition Table):

- _best move_
- _board signature (hash key)_
- _best move depth_
- _best move score_
- _best move performance_ , a new parameter you can calculate with any learning application supporting this specification. An example is the private one, kernel of SaaS part of [Alpha-Chess](http://www.alpha-chess.com) AI portal. The idea is to calculate it based on pattern recognition concept. In the portal, you can also exploit the reports of another NLG (virtual trainer) application and buy the products in the digishop based on all this. This open-source part has the performance default. So, it doesn't use it. Clearly, even if already strong, this private learning algorithm is a lot stronger as demostrate here: [Graphical result](https://github.com/amchess/BrainLearn/tree/master/tests/6-5.jpg)

This file is loaded in an hashtable at the engine load and updated each time the engine receive quit or stop uci command.
When BrainLearn starts a new game or when we have max 8 pieces on the chessboard, the learning is activated and the hash table updated each time the engine has a best score
at a depth >= 4 PLIES, according to Brainlearn aspiration window.

At the engine loading, there is an automatic merge to experience.exp files, if we put the other ones, based on the following convention:

&lt;fileType&gt;&lt;qualityIndex&gt;.exp

where

- _fileType=experience_
- _qualityIndex_ , an integer, incrementally from 0 on based on the file&#39;s quality assigned by the user (0 best quality and so on)

N.B.

Because of disk access, less time the engine can think, less effective is the learning.

Old versions had this experience file with a .bin extension, but now we added the bin book format support, so the extension is changed in .exp. So, old files can simply be renamed by changing this extension.

#### Contempt
The default value is 0 and keep it for analysis purpose. For game playing, you can use the default brainlearn value 24

#### Dynamic contempt

_Boolean, Default: True_ For match play, activate it and the engine uses the dynamic contempt. For analysis purpose; keep it at its default, to completely avoid, with contempt settled to 0, the well known [rollercoaster effect](http://talkchess.com/forum3/viewtopic.php?t=69129) and align so the engine's score to the gui's informator symbols

### Read only learning

_Boolean, Default: False_ 
If activated, the learning file is only read.

### Self Q-learning

_Boolean, Default: False_ 
If activated, the learning algorithm is the [Q-learning](https://youtu.be/qhRNvCVVJaA?list=PLZbbT5o_s2xoWNVdDudn51XM8lOuZ_Njv), optimized for self play. Some GUIs don't write the experience file in some game's modes because the uci protocol is differently implemented

### MonteCarlo Tree Search section (experimental: thanks to original Stephan Nicolet work)
#### MCTS (checkbox)

_Boolean, Default: False_ If activated, the engine uses the MonteCarlo Tree Search in the manner specified by the following parameters.

#### MCTSThreads

_Integer, Default: 0, Min: 0, Max: 512_
The number of settled threads to use for MCTS search except the first (main) one always for alpha-beta search. 
In particular, if the number is greater than threads number, they will all do a montecarlo tree search, always except the first (main) for alpha-beta search.

#### Multi Strategy 

_Integer, Default: 20, Min: 0, Max: 100_ 
Only in multi mcts mode, for tree policy.

#### Multi MinVisits

_Integer, Default: 5, Min: 0, Max: 1000_
Only in multi mcts mode, for Upper Confidence Bound.

### Book management section (thanks to Khalid Omar)
The order is: bin->ctg->live book

#### CTG/BIN Book 1 File
The file name of the first book file which could be a polyglot (BIN) or Chessbase (CTG) book. To disable this book, use: ```<empty>```
If the book (CTG or BIN) is in a different directory than the engine executable, then configure the full path of the book file, example:
```C:\Path\To\My\Book.ctg``` or ```/home/username/path/to/book/bin```

#### Book 1 Width
The number of moves to consider from the book for the same position. To play best book move, set this option to ```1```. If a value ```n``` (greater than ```1```) is configured, the engine will pick **randomly** one of the top ```n``` moves available in the book for the given position

#### Book 1 Depth
The maximum number of moves to play from the book
	
#### (CTG) Book 1 Only Green
This option is only used if the loaded book is a CTG book. When set to ```true```, the engine will only play Green moves from the book (if any). If no green moves found, then no book move is made
This option has no effect or use if the loaded book is a Polyglot (BIN) book
    
#### CTG/BIN Book 2 File
Same explaination as **CTG/BIN Book 1 File**, but for the second book

#### Book 2 Width
Same explaination as **BIN Book 1 Width**, but for the second book

#### Book 2 Depth
Same explaination as **BIN Book 1 Depth**, but for the second book

#### (CTG) Book 2 Only Green
Same explaination as **(CTG) Book 1 Only Green**, but for the second book

#### UCI commands
Polyfish supports all UCI commands supported by Brainlearn. *Click [here](https://github.com/official-stockfish/Brainlearn/blob/master/README.md#the-uci-protocol-and-available-options) to see the full list of supported Brainlearn UCI commands*

Polyfish also supports the following UCI commands

  * ##### book
    This command causes the engine to show available moves and associated information from the currently configured books
	```
	position startpos
	poly

	 +---+---+---+---+---+---+---+---+
	 | r | n | b | q | k | b | n | r | 8
	 +---+---+---+---+---+---+---+---+
	 | p | p | p | p | p | p | p | p | 7
	 +---+---+---+---+---+---+---+---+
	 |   |   |   |   |   |   |   |   | 6
	 +---+---+---+---+---+---+---+---+
	 |   |   |   |   |   |   |   |   | 5
	 +---+---+---+---+---+---+---+---+
	 |   |   |   |   |   |   |   |   | 4
	 +---+---+---+---+---+---+---+---+
	 |   |   |   |   |   |   |   |   | 3
	 +---+---+---+---+---+---+---+---+
	 | P | P | P | P | P | P | P | P | 2
	 +---+---+---+---+---+---+---+---+
	 | R | N | B | Q | K | B | N | R | 1
	 +---+---+---+---+---+---+---+---+
	   a   b   c   d   e   f   g   h

	Fen: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
	Key: 8F8F01D4562F59FB
	Checkers:

	Polyglot book 1: MyNarrowBook.bin
	1 : e2e4 , count: 8822
	2 : d2d4 , count: 6644

	Polyglot book 2: MyWideBook.bin
	1 : e2e4 , count: 9768
	2 : d2d4 , count: 5347
	3 : g1f3 , count: 1034
	4 : c2c4 , count: 965
	5 : b2b3 , count: 99
	6 : f2f4 , count: 94
	7 : g2g3 , count: 76
	8 : b2b4 , count: 43
	9 : e2e3 , count: 32
	10: b1c3 , count: 32
	11: d2d3 , count: 13
	12: c2c3 , count: 12
	13: a2a3 , count: 10
	14: g2g4 , count: 9
	15: h2h3 , count: 3
	16: h2h4 , count: 3
	17: a2a4 , count: 1
	18: g1h3 , count: 1
	19: b1a3 , count: 1
	20: f2f3 , count: 1
	```

#### Note about CTG books:
CTG book format specification is not available to the public from Chessbase. The code that reads and parses CTG books is based on the reverse engineered book specification published on [CTG Specifications](https://web.archive.org/web/20210129162445/https://rybkaforum.net/cgi-bin/rybkaforum/topic_show.pl?tid=2319) as well as the other resources mentioned earlier.

The reverse engineered specs are good enough to proble the book for moves, but it does not provide the same functionality as Chessbase own products. The following is a list of known limitations of **Polyfish**'s CTG book implementation:
- Does not support underpromotion, which means all promotion moves are assumed to be to a Queen
- Does not support more than two Queens on the board
- The logic that determines Green/Red moves is not 100% accurate which may cause the engine not to play certain Green moves because it cannot identify them.
- Some move annotations and engine recommendations can be read.

The move weight calculated by **Polyfish** (can be seen using the **book** command) is my own attempt to compensate for all the missing/unknown information about the CTG specification. Using the calculated weight, **Polyfish** can pick the best move for a given position in 99% of the times because it ustilizes existing move statistics such as number of wins, draws, and losses, as well as "known" annotations (!, !?, ?!, ??, OnlyMove, etc...) and recommendations (Green vs. Red) if available.

Despite the fact that Polyfish can read and play from CTG Book, it is not going to be identical to Chessbase own products since it is based on the partial CTG specification available publicly. Use at your own risk!

#### Live Book section (thanks to Eman's author Khalid Omar for windows builds)

##### Live Book (checkbox)

_Boolean, Default: False_ If activated, the engine uses the livebook as primary choice.

##### Live Book URL
The default is the online [chessdb](https://www.chessdb.cn/queryc_en/), a wonderful project by noobpwnftw (thanks to him!)
 
[https://github.com/noobpwnftw/chessdb](https://github.com/noobpwnftw/chessdb)

[http://talkchess.com/forum3/viewtopic.php?f=2&t=71764&hilit=chessdb](http://talkchess.com/forum3/viewtopic.php?f=2&t=71764&hilit=chessdb)

The private application can also learn from this live db.

##### Live Book Timeout

_Default 5000, min 0, max 10000_ Only for bullet games, use a lower value, for example, 1500.

##### Live Book Retry

_Default 3, min 1, max 100_ Max times the engine tries to contribute (if the corresponding option is activated: see below) to the live book. If 0, the engine doesn't use the livebook.

##### Live Book Diversity

_Boolean, Default: False_ If activated, the engine varies its play, reducing conversely its strength because already the live chessdb is very large.

##### Live Book Contribute

_Boolean, Default: False_ If activated, the engine sends a move, not in live chessdb, in its queue to be analysed. In this manner, we have a kind of learning cloud.

##### Live Book Depth

_Default 100, min 1, max 100_ Depth of live book moves.

### Opening variety

_Integer, Default: 0, Min: 0, Max: 40_
To play different opening lines from default (0), if not from book (see below).
Higher variety -> more probable loss of ELO

### Concurrent Experience

_Boolean, Default: False_ 
Set this option to true when running under CuteChess and you experiences problems with concurrency > 1
When this option is true, the saved experience file name will be modified to something like experience-64a4c665c57504a4.exp
(64a4c665c57504a4 is random). Each concurrent instance of BrainLearn will have its own experience file name, however, all the concurrent instances will read "experience.exp" at start up.

## pgn_to_bl_converter

Converting pgn to brainlearn format is really simple.

Requirements
1. Download cuteChess gui
2. Download brainlearn
3. Download brainlearn or equivalent
3. Download the pgn files you want to convert

In Cute Chess, set a tournament according to the photo in this link:
[CuteChess Settings](https://github.com/amchess/BrainLearn/tree/master/doc/pgn_to_bl.PNG)

- Add the 2 engines (One should be brainlearn)

-start a tournament with brainlearn and any other engine. It will convert all the games in the pgn file and save them to game.bin

Note: We recommend you use games from high quality play.

<h1 align="center">Brainlearn NNUE</h1>

## Compiling Brainlearn

Brainlearn has support for 32 or 64-bit CPUs, certain hardware instructions,
big-endian machines such as Power PC, and other platforms.

On Unix-like systems, it should be easy to compile Brainlearn directly from the
source code with the included Makefile in the folder `src`. In general, it is
recommended to run `make help` to see a list of make targets with corresponding
descriptions. An example suitable for most Intel and AMD chips:

```
cd src
make -j profile-build ARCH=x86-64-avx2
```

Detailed compilation instructions for all platforms can be found in our
[documentation][wiki-compile-link]. Our wiki also has information about
the [UCI commands][wiki-uci-link] supported by Brainlearn.

## Terms of use

Brainlearn is free and distributed under the
[**GNU General Public License version 3**][license-link] (GPL v3). Essentially,
this means you are free to do almost exactly what you want with the program,
including distributing it among your friends, making it available for download
from your website, selling it (either by itself or as part of some bigger
software package), or using it as the starting point for a software project of
your own.

The only real limitation is that whenever you distribute Brainlearn in some way,
you MUST always include the license and the full source code (or a pointer to
where the source code can be found) to generate the exact binary you are
distributing. If you make any changes to the source code, these changes must
also be made available under GPL v3.


[authors-link]:       https://github.com/official-stockfish/Stockfish/blob/master/AUTHORS
[build-link]:         https://github.com/official-stockfish/Stockfish/actions/workflows/stockfish.yml
[commits-link]:       https://github.com/official-stockfish/Stockfish/commits/master
[discord-link]:       https://discord.gg/GWDRS3kU6R
[issue-link]:         https://github.com/official-stockfish/Stockfish/issues/new?assignees=&labels=&template=BUG-REPORT.yml
[discussions-link]:   https://github.com/official-stockfish/Stockfish/discussions/new
[fishtest-link]:      https://tests.stockfishchess.org/tests
[guideline-link]:     https://github.com/official-stockfish/fishtest/wiki/Creating-my-first-test
[license-link]:       https://github.com/official-stockfish/Stockfish/blob/master/Copying.txt
[programming-link]:   https://www.chessprogramming.org/Main_Page
[programmingsf-link]: https://www.chessprogramming.org/Stockfish
[readme-link]:        https://github.com/official-stockfish/Stockfish/blob/master/README.md
[release-link]:       https://github.com/official-stockfish/Stockfish/releases/latest
[src-link]:           https://github.com/official-stockfish/Stockfish/tree/master/src
[stockfish128-logo]:  https://stockfishchess.org/images/logo/icon_128x128.png
[uci-link]:           https://backscattering.de/chess/uci/
[website-link]:       https://stockfishchess.org
[website-blog-link]:  https://stockfishchess.org/blog/
[wiki-link]:          https://github.com/official-stockfish/Stockfish/wiki
[wiki-compile-link]:  https://github.com/official-stockfish/Stockfish/wiki/Compiling-from-source
[wiki-uci-link]:      https://github.com/official-stockfish/Stockfish/wiki/UCI-&-Commands
[wiki-usage-link]:    https://github.com/official-stockfish/Stockfish/wiki/Download-and-usage
[worker-link]:        https://github.com/official-stockfish/fishtest/wiki/Running-the-worker

[build-badge]:        https://img.shields.io/github/actions/workflow/status/official-stockfish/Stockfish/stockfish.yml?branch=master&style=for-the-badge&label=stockfish&logo=github
[commits-badge]:      https://img.shields.io/github/commits-since/official-stockfish/Stockfish/latest?style=for-the-badge
[discord-badge]:      https://img.shields.io/discord/435943710472011776?style=for-the-badge&label=discord&logo=Discord
[fishtest-badge]:     https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=Fishtest&up_color=success&up_message=Online&url=https%3A%2F%2Ftests.stockfishchess.org%2Ftests%2Ffinished
[license-badge]:      https://img.shields.io/github/license/official-stockfish/Stockfish?style=for-the-badge&label=license&color=success
[release-badge]:      https://img.shields.io/github/v/release/official-stockfish/Stockfish?style=for-the-badge&label=official%20release
[website-badge]:      https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=website&up_color=success&up_message=Online&url=https%3A%2F%2Fstockfishchess.org
