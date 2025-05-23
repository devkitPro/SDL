/*
  Simple DirectMedia Layer
  Copyright (C) 2025 Daniel K. O. <dkosmari@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef SDL_cpp_allocator_h
#define SDL_cpp_allocator_h

#include <cstdlib>
#include <new>
#include <type_traits>

#include "SDL_stdinc.h"

/*
 * A custom allocator to use with C++ standard library containers.
 *
 * All allocations are done through SDL_malloc() and SDL_free().
 */
namespace
{
    namespace sdl
    {
        template <typename T>
        struct allocator
        {
            using value_type = T;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;

            using propagate_on_container_move_assignment = std::true_type;

            T *
            allocate(std::size_t count)
            {
                auto ptr = reinterpret_cast<T *>(SDL_malloc(count * sizeof(T)));
                if (!ptr)
                    throw std::bad_alloc{};
                return ptr;
            }

            void
            deallocate(T *ptr, std::size_t /*count*/)
            {
                SDL_free(ptr);
            }

            constexpr bool
            operator==(const allocator &) noexcept
            {
                return true;
            }
        };

    } // namespace sdl

} // namespace

#endif

/*
 * Local Variables:
 * mode: c++
 * End:
 */
