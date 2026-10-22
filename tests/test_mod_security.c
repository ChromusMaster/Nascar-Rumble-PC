#define nr_mods_open security_mods_open
#define nr_mods_attach security_mods_attach
#define nr_mods_frame security_mods_frame
#define nr_mods_close security_mods_close
#define nr_mods_export_text security_mods_export_text
#include "../src/mods/mods.c"
#ifndef _WIN32
#include <unistd.h>
#endif
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    nr_mods m = {0};
    char *text;
    const char *catalog = "\xEF\xBB\xBF[fe]\n100 = Test\n100 = Duplicate\n2147483648 = Overflow\n101 = \\x00\n102 = \\xZZ\n103 = \xF0\x9F\x98\x80\n104 = valid\n";
    int i;
    const char *invalid[] = {"\\x00", "\\xGG", "\\x", "\\", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82", "\xE4\xB8\xAD"};
    for (i = 0; i < (int)NR_ARRAY_COUNT(invalid); i++) REQUIRE(encode(invalid[i], false) == NULL);
    text = encode("a\\n\\x13\\\\\xC3\xA7", false);
    REQUIRE(text && strcmp(text, "a\n\x13\\c") == 0);
    free(text);
    REQUIRE(nr_write_file("security-text.txt", catalog, strlen(catalog)));
    load_text(&m, "security-text.txt");
    REQUIRE(m.nfe == 2 && m.fe[0].id == 100 && m.fe[1].id == 104);
    for (i = 0; i < m.nfe; i++) free(m.fe[i].text);
    free(m.fe);
    remove("security-text.txt");
    REQUIRE(!nr_safe_relative_path(".", "../escape"));
    REQUIRE(!nr_safe_relative_path(".", "NUL.txt"));
    REQUIRE(!nr_safe_relative_path(".", "files/COM1"));
    REQUIRE(!nr_safe_relative_path(".", "/absolute"));
    REQUIRE(!nr_safe_relative_path(".", "a/../b"));
    REQUIRE(!nr_safe_relative_path(".", "C:\\bad"));
    REQUIRE(nr_safe_relative_path(".", "Language/user/text.txt"));
#ifndef _WIN32
    remove("security-link");
    REQUIRE(symlink("/tmp", "security-link") == 0);
    REQUIRE(!nr_safe_relative_path(".", "security-link/text.txt"));
    remove("security-link");
#endif
    puts("mod security: passed");
    return 0;
}
