/*
 @ Kingsley Chen
*/

#include "kbase\strings\string_format.h"

#include <algorithm>
#include <cctype>

#include "kbase\scope_guard.h"

namespace {

using kbase::StringFormatSpecifierError;
using kbase::internal::FmtStr;
using kbase::internal::Placeholder;
using kbase::internal::PlaceholderList;

enum class FormatStringParseState {
    IN_TEXT,
    IN_FORMAT
};

const char kEscapeBegin = '{';
const char kEscapeEnd = '}';
const char kSpecifierDelimeter = ':';
const char kPlaceholderChar = '@';

inline bool IsDigit(char ch)
{
    return isdigit(ch) != 0;
}

inline bool IsDigit(wchar_t ch)
{
    return iswdigit(ch) != 0;
}

inline unsigned long ExtractPlaceholderIndex(const char* first_digit, char** last_digit)
{
    auto index = strtoul(first_digit, last_digit, 10);
    --*last_digit;

    return index;
}

inline unsigned long ExtractPlaceholderIndex(const wchar_t* first_digit,
                                             wchar_t** last_digit)
{
    auto index = wcstoul(first_digit, last_digit, 10);
    --*last_digit;

    return index;
}

// This wrapper provides a more semantic validation.
inline void EnsureFormatSpecifier(bool expr)
{
    if (!expr) {
        throw StringFormatSpecifierError("Format string is not valid");
    }
}

template<typename CharT>
typename FmtStr<CharT>::String AnalyzeFormatStringT(const CharT* fmt,
                                                    PlaceholderList<CharT>* placeholders)
{
    const size_t kInitialCapacity = 32;
    typename FmtStr<CharT>::String analyzed_fmt;
    analyzed_fmt.reserve(kInitialCapacity);

    placeholders->clear();
    Placeholder<CharT> placeholder;

    auto state = FormatStringParseState::IN_TEXT;
    for (auto ptr = fmt; *ptr != '\0'; ++ptr) {
        if (*ptr == kEscapeBegin) {
            // `{` is an invalid token for format-state.
            EnsureFormatSpecifier(state != FormatStringParseState::IN_FORMAT);

            if (*(ptr + 1) == kEscapeBegin) {
                // Use `{{` to represent literal `{`.
                analyzed_fmt += kEscapeBegin;
                ++ptr;
            } else if (IsDigit(*(ptr + 1))) {
                CharT* last_digit;
                placeholder.index = ExtractPlaceholderIndex(ptr + 1, &last_digit);
                ptr = last_digit;
                EnsureFormatSpecifier(*(ptr + 1) == kEscapeEnd ||
                                      *(ptr + 1) == kSpecifierDelimeter);
                if (*(ptr + 1) == kSpecifierDelimeter) {
                    ++ptr;
                }

                // Get into format-state.
                state = FormatStringParseState::IN_FORMAT;
            } else {
                throw StringFormatSpecifierError("Format string is not valid");
            }
        } else if (*ptr == kEscapeEnd) {
            if (state == FormatStringParseState::IN_TEXT) {
                EnsureFormatSpecifier(*(ptr + 1) == kEscapeEnd);
                analyzed_fmt += kEscapeEnd;
                ++ptr;
            } else {
                placeholder.pos = analyzed_fmt.length();
                analyzed_fmt += '@';
                placeholders->push_back(placeholder);

                // Now we turn back to text-state.
                state = FormatStringParseState::IN_TEXT;
            }
        } else {
            if (state == FormatStringParseState::IN_TEXT) {
                analyzed_fmt += *ptr;
            } else {
                placeholder.format_specifier += *ptr;
            }
        }
    }

    EnsureFormatSpecifier(state == FormatStringParseState::IN_TEXT);

    std::sort(std::begin(*placeholders), std::end(*placeholders),
              [](const Placeholder<CharT>& lhs, const Placeholder<CharT>& rhs) {
        return lhs.index < rhs.index;
    });

    return analyzed_fmt;
}

} // namespace

namespace kbase {

namespace internal {

std::string AnalyzeFormatString(const char* fmt, PlaceholderList<char>* placeholders)
{
    return AnalyzeFormatStringT(fmt, placeholders);
}

std::wstring AnalyzeFormatString(const wchar_t* fmt,
                                 PlaceholderList<wchar_t>* placeholders)
{
    return AnalyzeFormatStringT(fmt, placeholders);
}

}   // namespace internal

inline int vsnprintfT(char* buf, size_t buf_size, size_t count_to_write,
                      const char* fmt, va_list args)
{
    return vsnprintf_s(buf, buf_size, count_to_write, fmt, args);
}

inline int vsnprintfT(wchar_t* buf, size_t buf_size, size_t count_to_write,
                      const wchar_t* fmt, va_list args)
{
    return _vsnwprintf_s(buf, buf_size, count_to_write, fmt, args);
}

template<typename strT>
void StringAppendPrintfT(strT* str, const typename strT::value_type* fmt, va_list ap)
{
    typedef typename strT::value_type charT;

    const int kDefaultCharCount = 1024;
    charT buf[kDefaultCharCount];

    int ret = vsnprintfT(buf, kDefaultCharCount, kDefaultCharCount - 1, fmt, ap);

    if (ret >= 0) {
        str->append(buf, ret);
        return;
    }

    // data is truncated.
    // adjust the buffer size until it fits

    const int kMaxAllowedCharCount = (1 << 25);
    int tentative_char_count = kDefaultCharCount;
    while (true) {
        tentative_char_count <<= 1;
        if (tentative_char_count > kMaxAllowedCharCount) {
            throw StringPrintfDataLengthError("memory needed exceeds the threshold");
        }

        std::vector<charT> dynamic_buf(tentative_char_count);

        // vsnprintf-like functions on Windows don't change the |ap|
        // while their counterparts on Linux do.
        // if you use VS2013 or higher, or compilers that support C99
        // you alternatively can use |va_copy| to make a copy of |ap|
        // during each iteration.
        int ret = vsnprintfT(&dynamic_buf[0], tentative_char_count,
                             tentative_char_count - 1, fmt, ap);
        if (ret >= 0) {
            str->append(&dynamic_buf[0], ret);
            return;
        }
    }
}

void StringAppendPrintf(std::string* str, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });
    StringAppendPrintfT(str, fmt, args);
}

void StringAppendPrintf(std::wstring* str, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });
    StringAppendPrintfT(str, fmt, args);
}

std::string StringPrintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });

    std::string str;
    StringAppendPrintfT(&str, fmt, args);

    return str;
}

std::wstring StringPrintf(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });

    std::wstring str;
    StringAppendPrintfT(&str, fmt, args);

    return str;
}

const std::string& StringPrintf(std::string* str, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });

    str->clear();
    StringAppendPrintfT(str, fmt, args);

    return *str;
}

const std::wstring& StringPrintf(std::wstring* str, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ON_SCOPE_EXIT([&] { va_end(args); });

    str->clear();
    StringAppendPrintfT(str, fmt, args);

    return *str;
}

}   // namespace kbase