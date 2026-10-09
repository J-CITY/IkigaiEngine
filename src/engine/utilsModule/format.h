#pragma once
// Removed <format> to support iOS < 16
#include <sstream>
#include <string_view>

namespace IKIGAI::UTILS
{
//#ifdef USING_GLES
    template<typename T>
    void format_helper(std::ostringstream& oss, std::string_view& str, const T& value) {
        std::size_t openBracket = str.find('{');
        if (openBracket == std::string::npos) { return; }
        std::size_t closeBracket = str.find('}', openBracket + 1);
        if (closeBracket == std::string::npos) { return; }
        oss << str.substr(0, openBracket) << value;
        str = str.substr(closeBracket + 1);
    }
//#endif

    template<typename... Targs>
    std::string format(std::string_view str, Targs...args) {
//#ifdef USING_GLES
        std::ostringstream oss;
        (format_helper(oss, str, args), ...);
        oss << str;
        return oss.str();
//#else
//        return std::format(str, std::make_format_args(std::forward<Targs>(args)...));
//#endif
    }
}
