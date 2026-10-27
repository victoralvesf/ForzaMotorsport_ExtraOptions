// md2txt <in.md> <out.txt>: the README as plain text for the release zip (end users open it in Notepad).
// Headings are underlined (= for #, - for the rest), **bold**, `code` and code fences are dropped, "> " quotes
// lose the marker, and tables become aligned columns with the last one wrapped under itself. Output is CRLF.
#include "md2txt.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: md2txt <in.md> <out.txt>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "md2txt: cannot read %s\n", argv[1]);
        return 1;
    }
    std::string md((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::ofstream out(argv[2], std::ios::binary);
    out << md2txt::convert(md);
    if (!out) {
        std::fprintf(stderr, "md2txt: cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
