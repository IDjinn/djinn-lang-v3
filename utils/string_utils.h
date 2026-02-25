//
// Created by Luke on 13/12/2025.
//

#ifndef DJINN_STRING_UTILS_H
#define DJINN_STRING_UTILS_H


#include <iomanip>
#include <sstream>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>

namespace string_utils
{
    inline std::string llvm_type_str(const llvm::Type* type)
    {
        std::string str;
        llvm::raw_string_ostream os(str);
        type->print(os);
        return str;
    }

    inline std::string escape_visible(const std::string& input)
    {
        std::ostringstream ss;

        for (const unsigned char c : input)
        {
            switch (c)
            {
            case '\n': ss << "\\n";
                break;
            case '\r': ss << "\\r";
                break;
            case '\t': ss << "\\t";
                break;
            case '\b': ss << "\\b";
                break;
            case '\f': ss << "\\f";
                break;
            case '\\': ss << "\\\\";
                break;
            case '"': ss << "\\\"";
                break;

            default:
                if (c < 0x20 || c > 0x7E)
                {
                    ss << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                }
                else
                {
                    ss << c;
                }
            }
        }

        return ss.str();
    }
}

#endif //DJINN_STRING_UTILS_H