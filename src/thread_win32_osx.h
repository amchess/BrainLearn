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

#ifndef THREAD_WIN32_OSX_H_INCLUDED
#define THREAD_WIN32_OSX_H_INCLUDED

#include <thread>

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches, which require
// somewhat more than 1MB stack, so adjust it to TH_STACK_SIZE.
// The implementation calls pthread_create() with the stack size parameter
// equal to the Linux 8MB default, on platforms that support it.

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)

    #include <pthread.h>

namespace Brainlearn {

static const size_t TH_STACK_SIZE = 8 * 1024 * 1024;

template<class T, class P = std::pair<T*, void (T::*)()>>
void* start_routine(void* ptr) {
    P* p = reinterpret_cast<P*>(ptr);
    (p->first->*(p->second))();  // Call member function pointer
    delete p;
    return nullptr;
}

class NativeThread {

    pthread_t thread;

   public:
    template<class T, class P = std::pair<T*, void (T::*)()>>
    explicit NativeThread(void (T::*fun)(), T* obj) {
        pthread_attr_t attr_storage, *attr = &attr_storage;
        pthread_attr_init(attr);
        pthread_attr_setstacksize(attr, TH_STACK_SIZE);
        pthread_create(&thread, attr, start_routine<T>, new P(obj, fun));
    }
    void join() { pthread_join(thread, nullptr); }
};

}  // namespace Brainlearn

#else  // Default case: use STL classes

namespace Brainlearn {

using NativeThread = std::thread;

}  // namespace Brainlearn

#endif

#endif  // #ifndef THREAD_WIN32_OSX_H_INCLUDED
