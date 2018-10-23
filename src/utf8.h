#ifndef BKH_UTF8_H
#define BKH_UTF8_H
#pragma once

/** utf8.h - Bendik Hillestad - Public Domain
 * Provides a fast table-based DFA for decoding sequences of UTF-8 code
 * units into UTF-32 code points. Based on the work of Björn Höhrmann
 * and Bob Steagall. Written using modern C++.
 *
 * This implementation exposes a single generic function that takes a 
 * container of code units and writes out the resulting UTF-32 code
 * points to a buffer.
 *
 * =======================================================================
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <cstdint>
#include <type_traits>

namespace bkh::encoding
{
    namespace detail
    {
		/* Workaround for GCC and Clang */

        template<typename T>
        struct dependent_false : std::false_type{};
		
        /* Feature tests */

        //Test if there's an inequality comparison operator that accepts const references
        template<typename Lhs, typename Rhs>
        static auto is_inequality_comparable(Lhs const& lhs, Rhs const& rhs) -> decltype(lhs != rhs, std::true_type{});
        static auto is_inequality_comparable(...)                            -> std::false_type;

        //Test if we can dereference the iterator and then advance it
        template<typename InputIt>
        static auto is_readable(InputIt input)                               -> decltype(*input, ++input, std::true_type{});
        static auto is_readable(...)                                         -> std::false_type;

        //Test if we can write through the iterator and then advance it
        template<typename OutputIt>
        static auto is_writeable(OutputIt output, char32_t value)            -> decltype(*output = value, ++output, std::true_type{});
        static auto is_writeable(...)                                        -> std::false_type;

        /* Helper functions */

        //Determines if iteration can continue
        template<typename InputIt>
        inline bool check(InputIt const& input, InputIt const& end) noexcept(true /* TODO */)
        {
            //Check if valid input iterators
            if constexpr(decltype(is_inequality_comparable(input, end)){})
            {
                return input != end;
            } 
            else static_assert(dependent_false<InputIt>{}, "'input' and 'end' must be comparable");
        }

        //Reads an octet from the input and advances the iterator
        template<typename InputIt>
        inline std::uint8_t read(InputIt& input) noexcept(true /* TODO */)
        {
            //Check if valid input iterator
            if constexpr(decltype(is_readable(input)){})
            {
                //Determine the iterator's value type
                using value_type = std::remove_cv_t
                <
                    std::remove_reference_t<decltype(*input)>
                >;

                //Check if the value type is valid
                if constexpr((sizeof(value_type) == 1) && std::is_convertible_v<value_type, std::uint8_t>)
                {
                    auto octet = static_cast<std::uint8_t>(*input);
                    ++input;
                    return octet;
                }
                else static_assert(dependent_false<InputIt>{}, "'input' has a bad value type");
            }
            else static_assert(dependent_false<InputIt>{}, "'input' must be readable");
        }

        //Writes a char32_t to the output and advances the iterator
        template<typename OutputIt>
        inline void write(OutputIt& output, char32_t value) noexcept(true /* TODO */)
        {
            //Check if valid output iterator
            if constexpr(decltype(is_writeable(output, value)){})
            {
                *output = value;
                ++output;
            }
            else static_assert(dependent_false<OutputIt>{}, "'output' must be writable");
        }
    };

    class utf8
    {
    public:
        using uint8_t = std::uint8_t;

        template<typename InputIt, typename OutputIt>
        static inline InputIt decode(InputIt input, InputIt end, OutputIt output) noexcept(true /* TODO */)
        {
            //Loop until we reach the end
            while (detail::check(input, end))
            {
                //Read an octet and advance the iterator
                uint8_t octet = detail::read(input);

                //Check for ASCII
                if (octet < 0x80)
                {
                    detail::write(output, static_cast<char32_t>(octet));
                }
                else
                {
                    //Check for invalid value
                    if (octet < 0xC2) return input;

                    //Get the initial state
                    auto [tmp, state]   = lookup.initial_state[octet - 0xC2];
                    char32_t code_point = static_cast<char32_t>(tmp);

                    //Continue until we hit either an accepting state or error state
                    while (state > ERR)
                    {
                        //Check if we may continue
                        if (detail::check(input, end))
                        {
                            //Read another octet
                            octet = detail::read(input);

                            //Update the code point
                            code_point = (code_point << 6) | (octet & 0x3F);

                            //Get the category and update state
                            auto category = lookup.octet_category[octet];
                            state = lookup.state_transition[state + category];
                        }
                        else state = ERR;
                    }

                    //Check if we hit an invalid state
                    if (state == ERR) return input;

                    //Write the code point
                    detail::write(output, code_point);
                }
            }

            return end;
        }

        utf8() = delete;

    private:

        enum character_class_t : uint8_t
        {
            ILL = 0, ASC = 1, CR1 =  2, CR2 =  3,
            CR3 = 4, L2A = 5, L3A =  6, L3B =  7,
            L3C = 8, L4A = 9, L4B = 10, L4C = 11
        };

        enum dfa_state_t : uint8_t
        {
            BGN =   0, ERR =  12,
            CS1 =  24, CS2 =  36,
            CS3 =  48, P3A =  60,
            P3B =  72, P4A =  84,
            P4B =  96, END = BGN
        };

        static inline struct alignas(512) _lookup
        {
			_lookup(){}; //Workaround for bug in Clang and GCC
			
            struct { uint8_t code_point; dfa_state_t state; } initial_state[62]
            {
                { 0x02, CS1 }, { 0x03, CS1 }, { 0x04, CS1 }, { 0x05, CS1 },
                { 0x06, CS1 }, { 0x07, CS1 }, { 0x08, CS1 }, { 0x09, CS1 },
                { 0x0A, CS1 }, { 0x0B, CS1 }, { 0x0C, CS1 }, { 0x0D, CS1 },
                { 0x0E, CS1 }, { 0x0F, CS1 }, { 0x10, CS1 }, { 0x11, CS1 },
                { 0x12, CS1 }, { 0x13, CS1 }, { 0x14, CS1 }, { 0x15, CS1 },
                { 0x16, CS1 }, { 0x17, CS1 }, { 0x18, CS1 }, { 0x19, CS1 },
                { 0x1A, CS1 }, { 0x1B, CS1 }, { 0x1C, CS1 }, { 0x1D, CS1 },
                { 0x1E, CS1 }, { 0x1F, CS1 }, { 0x00, P3A }, { 0x01, CS2 },
                { 0x02, CS2 }, { 0x03, CS2 }, { 0x04, CS2 }, { 0x05, CS2 },
                { 0x06, CS2 }, { 0x07, CS2 }, { 0x08, CS2 }, { 0x09, CS2 },
                { 0x0A, CS2 }, { 0x0B, CS2 }, { 0x0C, CS2 }, { 0x0D, P3B },
                { 0x0E, CS2 }, { 0x0F, CS2 }, { 0x00, P4A }, { 0x01, CS3 },
                { 0x02, CS3 }, { 0x03, CS3 }, { 0x04, P4B }, { 0xF5, ERR },
                { 0xF6, ERR }, { 0xF7, ERR }, { 0xF8, ERR }, { 0xF9, ERR },
                { 0xFA, ERR }, { 0xFB, ERR }, { 0xFC, ERR }, { 0xFD, ERR },
                { 0xFE, ERR }, { 0xFF, ERR }
            };

            character_class_t octet_category[256]
            {
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC, ASC,
                CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1, CR1,
                CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2, CR2,
                CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3,
                CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3, CR3,
                ILL, ILL, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A,
                L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A, L2A,
                L3A, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3B, L3C, L3B, L3B,
                L4A, L4B, L4B, L4B, L4C, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL, ILL,
            };

            dfa_state_t state_transition[108]
            {
                ERR, END, ERR, ERR, ERR, CS1, P3A, CS2, P3B, P4A, CS3, P4B,
                ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, END, END, END, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, CS1, CS1, CS1, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, CS2, CS2, CS2, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, ERR, ERR, CS1, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, CS1, CS1, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, ERR, CS2, CS2, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
                ERR, ERR, CS2, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR, ERR,
            };

            uint8_t pad[24]{};
        } const lookup{};
    };
};

#endif
