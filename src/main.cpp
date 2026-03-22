#include "app.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc == 2) {
        std::string arg = argv[1];
        auto at = arg.find('@');
        if (at == std::string::npos) {
            fprintf(stderr, "Usage: lazywire [user@host]\n");
            return 1;
        }
        std::string user = arg.substr(0, at);
        std::string host = arg.substr(at + 1);

        // Prompt for password without echo, like ssh
        std::string prompt = user + "@" + host + "'s password: ";
        char* pw = getpass(prompt.c_str());
        std::string password = pw ? pw : "";
        // Clear the password from memory
        if (pw) memset(pw, 0, strlen(pw));

        App app(user, host, password);
        return app.run();
    }

    App app;
    return app.run();
}
