#pragma once

#include <string>

#include <base64.hpp>


namespace fptn::common::utils::base64
{
    inline std::string decode(const std::string& s)
    {
        // If the input string's length is not a multiple of 4,
        // it appends '=' to make the length valid for base64 decoding.
        std::string additional;
        if (s.size() % 4 != 0) {
            const std::size_t padding = 4 - (s.size() % 4);
            for (std::size_t i = 0; i < padding; i++) {
                additional += "=";
            }
        }
        return ::base64::from_base64(s + additional);
    }
}