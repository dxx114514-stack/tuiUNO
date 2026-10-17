#pragma once

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace uno {

enum class Lang { En, Zh };

// Convenience: stringify any streamable value for message interpolation.
template <typename T>
std::string S(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

class I18n {
  public:
    static I18n& instance();

    Lang lang() const { return lang_; }
    void setLang(Lang l) { lang_ = l; }
    void toggle() { lang_ = (lang_ == Lang::En) ? Lang::Zh : Lang::En; }
    bool isZh() const { return lang_ == Lang::Zh; }

    // Look up a key. Falls back to English, then to the key itself.
    const std::string& tr(const char* key) const;
    std::string fmt(const char* key) const;

  private:
    I18n();
    Lang lang_ = Lang::En;
};

const std::string& tr(const char* key);
std::string fmt(const char* key);

// Substitute {} placeholders left to right.
template <typename... Args>
std::string Fmt(const char* key, Args&&... args) {
    std::string s = fmt(key);
    std::vector<std::string> vals;
    vals.reserve(sizeof...(Args));
    (void)std::initializer_list<int>{
        (vals.push_back(S(std::forward<Args>(args))), 0)...};
    size_t vi = 0;
    size_t p = 0;
    while (vi < vals.size() && (p = s.find("{}", p)) != std::string::npos) {
        s.replace(p, 2, vals[vi++]);
        p += vals[vi - 1].size();
    }
    return s;
}

Lang parseLang(const std::string& s);

}  // namespace uno