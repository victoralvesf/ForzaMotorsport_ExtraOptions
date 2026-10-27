#include "md2txt.hpp"

#include <algorithm>
#include <vector>

namespace md2txt {

namespace {

using Row = std::vector<std::string>;

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Drops ** and the backticks of inline code.
std::string inline_text(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '`') continue;
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') {
            i++;
            continue;
        }
        r += s[i];
    }
    return r;
}

// Code points, so the box characters of the README count as one column.
size_t width(const std::string &s)
{
    return static_cast<size_t>(std::count_if(s.begin(), s.end(), [](char c) { return (c & 0xc0) != 0x80; }));
}

bool is_table_row(const std::string &line)
{
    std::string t = trim(line);
    return t.size() >= 2 && t.front() == '|' && t.back() == '|';
}

Row cells(const std::string &line)
{
    std::string t = trim(line);
    Row r;
    size_t start = 1;
    for (size_t i = 1; i < t.size(); i++) {
        if (t[i] != '|') continue;
        r.push_back(inline_text(trim(t.substr(start, i - start))));
        start = i + 1;
    }
    return r;
}

bool is_separator(const Row &r)
{
    for (const std::string &c : r)
        if (c.empty() || c.find_first_not_of("-:") != std::string::npos) return false;
    return !r.empty();
}

// Splits text into lines of at most max columns, at spaces (a longer word stays whole).
std::vector<std::string> wrap(const std::string &text, size_t max)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t sp = text.find(' ', pos);
        std::string word = text.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
        pos = sp == std::string::npos ? text.size() : sp + 1;
        if (word.empty()) continue;
        if (!cur.empty() && width(cur) + 1 + width(word) > max) {
            lines.push_back(cur);
            cur.clear();
        }
        cur += (cur.empty() ? "" : " ") + word;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);
    return lines;
}

void table(const std::vector<Row> &rows, std::vector<std::string> &out)
{
    size_t n = 0;
    for (const Row &r : rows) n = std::max(n, r.size());
    std::vector<size_t> w(n, 0);
    for (const Row &r : rows)
        for (size_t i = 0; i + 1 < r.size(); i++) w[i] = std::max(w[i], width(r[i]));
    size_t indent = 0;
    for (size_t i = 0; i + 1 < n; i++) indent += w[i] + 2;
    size_t last = k_width > indent + 20 ? k_width - indent : 20;

    for (size_t k = 0; k < rows.size(); k++) {
        const Row &r = rows[k];
        std::string head;
        for (size_t i = 0; i + 1 < n; i++) {
            std::string c = i < r.size() ? r[i] : "";
            head += c + std::string(w[i] + 2 - width(c), ' ');
        }
        std::vector<std::string> text = wrap(r.size() == n ? r[n - 1] : "", last);
        for (size_t j = 0; j < text.size(); j++) {
            std::string line = (j == 0 ? head : std::string(indent, ' ')) + text[j];
            out.push_back(line.substr(0, line.find_last_not_of(' ') + 1));
        }
        if (k == 0) { // header underline
            std::string u;
            for (size_t i = 0; i + 1 < n; i++) u += std::string(w[i], '-') + "  ";
            out.push_back(u + std::string(std::min(last, std::max<size_t>(width(r.back()), 1)), '-'));
        }
    }
}

} // namespace

std::string convert(const std::string &md)
{
    std::vector<std::string> in;
    size_t pos = 0;
    while (pos <= md.size()) {
        size_t nl = md.find('\n', pos);
        std::string line = md.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        in.push_back(line);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    while (!in.empty() && in.back().empty()) in.pop_back();

    std::vector<std::string> out;
    bool code = false;
    for (size_t i = 0; i < in.size(); i++) {
        const std::string &line = in[i];
        if (trim(line).rfind("```", 0) == 0) {
            code = !code;
            continue;
        }
        if (code) {
            out.push_back(line);
            continue;
        }
        if (is_table_row(line)) {
            std::vector<Row> rows;
            for (; i < in.size() && is_table_row(in[i]); i++) {
                Row r = cells(in[i]);
                if (!is_separator(r)) rows.push_back(r);
            }
            i--;
            table(rows, out);
            continue;
        }
        size_t hashes = line.find_first_not_of('#');
        if (hashes > 0 && hashes != std::string::npos && line[hashes] == ' ') {
            std::string title = inline_text(trim(line.substr(hashes)));
            out.push_back(title);
            out.push_back(std::string(width(title), hashes == 1 ? '=' : '-'));
            continue;
        }
        std::string t = line;
        if (t.rfind("> ", 0) == 0) t = t.substr(2);
        else if (t == ">") t.clear();
        out.push_back(inline_text(t));
    }

    std::string txt;
    for (const std::string &l : out) txt += l + "\r\n";
    return txt;
}

} // namespace md2txt
