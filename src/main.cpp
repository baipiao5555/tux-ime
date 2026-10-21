#include "server.h"

int main() {
    tuxime::Server server;
    if (!server.initialize()) {
        return 1;
    }
    return server.run();
}
