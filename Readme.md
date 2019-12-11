## Overview

BrainLearn is a free, powerful UCI chess engine derived from BrainFish ([https://zipproth.de/Brainfish/download/](https://zipproth.de/Brainfish/download/)). It is not a complete chess program and requires a UCI-compatible GUI (e.g. XBoard with PolyGlot, Scid, Cute Chess, eboard, Arena, Sigma Chess, Shredder, Chess Partner or Fritz) in order to be used comfortably. Read the documentation for your GUI of choice for information about how to use Stockfish with it.

## Files

This distribution of BrainLearn consists of the following files:

- Readme.md, the file you are currently reading.
- Copying.txt, a text file containing the GNU General Public License version 3.
- src, a subdirectory containing the full source code, including a Makefile that can be used to compile BrainLearn on Unix-like systems.

## UCI parameters

BrainLearn hash the same options as BrainFish, but it implements a persisted learning algorithm, managing a file named experience.bin.

It is a collection of one or more positions stored with the following format (similar to in memory Stockfish Transposition Table):

- _best move_
- _board signature (hash key)_
- _best move depth_
- _best move score_
- _best move performance_ , the new parameter calculated based on pattern recognition concept via a private offline learning application. Not having it, the default performance is 0 (not applied). This new learning algorithm is a lot stronger than the previous one as demostrate here: [Graphical result](https://github.com/amchess/BrainLearn/blob/master/Tests/6-5.jpg)

This file is loaded in an hashtable at the engine load and updated each time the engine receive quit or stop uci command.
When BrainLearn starts a new game or when we have max 8 pieces on the chessboard, the learning is activated and the hash table updated each time the engine has a best score
at a depth >= 4 PLIES, according to Stockfish aspiration window.

At the engine loading, there is an automatic merge to experience.bin files, if we put the other ones, based on the following convention:

&lt;fileType&gt;&lt;qualityIndex&gt;.bin

where

- _fileType=&quot;experience&quot;/&quot;bin&quot;_
- _qualityIndex_ , an integer, incrementally from 0 on based on the file&#39;s quality assigned by the user (0 best quality and so on)

N.B.

Because of disk access, to be effective, the learning must be made at no bullet time controls (less than 5 minutes/game).

### Live Book section (thanks to Eman's author Khalid Omar for windows builds)

#### Live Book (checkbox)

_Boolean, Default: False_ If activated, the engine uses the livebook as primary choice.

#### Live Book URL
The default is the online chessdb [https://www.chessdb.cn/queryc_en/](https://www.chessdb.cn/queryc_en/), a wonderful project by noobpwnftw (thanks to him!)
 
[https://github.com/noobpwnftw/chessdb](https://github.com/noobpwnftw/chessdb)
[http://talkchess.com/forum3/viewtopic.php?f=2&t=71764&hilit=chessdb](http://talkchess.com/forum3/viewtopic.php?f=2&t=71764&hilit=chessdb)

The private application can also learn from this live db.

#### Live Book Timeout

_Default 1500, min 0, max 10000_

#### Live Book Diversity

_Boolean, Default: False_ If activated, the engine varies its play, reducing conversely its strength because already the live chessdb is very large.

#### Live Book Contribute

_Boolean, Default: False_ If activated, the engine sends a move, not in live chessdb, in its queue to be analysed. In this manner, we have a kind of learning cloud.


### Opening variety

_Integer, Default: 0, Min: 0, Max: 40_
To play different opening lines from default (0), if not from book (see below).
Higher variety -> more probable loss of ELO

## pgn_to_bl_converter

Converting pgn to brainlearn format is really simple.

Requirements
1. Download cuteChess gui
2. Download brainlearn
3. Download stockfish or equivalent
3. Download the pgn files you want to convert

In Cute Chess, set a tournament according to the photo in this link:
[CuteChess Settings](https://github.com/amchess/BrainLearn/tree/master/doc/pgn_to_bl.PNG)

- Add the 2 engines (One should be brainlearn)

-start a tournament with brainlearn and any other engine. It will convert all the games in the pgn file and save them to game.bin

Note: We recommend you use games from high quality play.

## Terms of use

BrainLearn is free, and distributed under the **GNU General Public License version 3** (GPL v3). Essentially, this means that you are free to do almost exactly what you want with the program, including distributing it among your friends, making it available for download from your web site, selling it (either by itself or as part of some bigger software package), or using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute BrainLearn in some way, you must always include the full source code, or a pointer to where the source code can be found. If you make any changes to the source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL v3 found in the file named _Copying.txt_.