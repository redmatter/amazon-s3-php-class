#include <signal.h>
#include <rml/logger.h>

#include "siginit.h"

namespace app {

extern bool quit;

}

/* save+quit */
void sigint(int s) {
	LogError("SIGINT: Trying a clean exit");
	app::quit = true;
}

/* quit */
void sighup(int s) {
	LogError("SIGHUPed");
}

/* kill */
void sigterm(int s) {
	LogError("SIGTERM: Trying a clean exit");
	app::quit = true;
}

/* save */
void sigusr1(int s) {
	LogError("SIGUSR1 Ignored");
}

/* reset all */
void sigusr2(int s) {
	LogError("SIGUSR2 Ignored");
}

/* sigpipe ?! */
void sigpipe(int s) {
	LogError("SIGPIP Ignored");
}

void siginit(void) {
	signal(SIGINT, (__sighandler_t)sigint );
	signal(SIGHUP, (__sighandler_t)sighup );
	signal(SIGUSR1,(__sighandler_t)sigusr1);
	signal(SIGUSR2,(__sighandler_t)sigusr2);
	signal(SIGPIPE,(__sighandler_t)sigpipe);
	signal(SIGTERM,(__sighandler_t)sigterm);
}


