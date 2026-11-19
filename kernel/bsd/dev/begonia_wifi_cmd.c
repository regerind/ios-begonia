/*
 * begonia: the Wi-Fi driver's commands (begonia_wifi.c, begonia_wifi_link: status, detect, on, wmt, fw, cap, init, up,
 * scan [2g] [SSID], join, sta, ring, leave, off) from user space, for test sessions nobody is at: the driver's own
 * messages come at boot and are long gone from the log ring when it is read.
 *
 *   sysctl kern.begonia_wifi = "COMMAND"   runs it; reading kern.begonia_wifi gives what it said (up to 8 KB)
 *   /redwood/bin/wifi_cmd (tests/wifi_cmd.c) runs a sequence and prints every answer
 */
#include <libkern/libkern.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/locks.h>

void begonia_wifi_link(const char *args, char *out, size_t len);

static char answer[8192];
static LCK_GRP_DECLARE(wifi_cmd_grp, "begonia_wifi_cmd");
static LCK_MTX_DECLARE(wifi_cmd_mtx, &wifi_cmd_grp);

static int
sysctl_begonia_wifi SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char command[128];
	int error;

	lck_mtx_lock(&wifi_cmd_mtx);
	if (req->newptr != USER_ADDR_NULL) {
		size_t length = req->newlen < sizeof(command) - 1 ? req->newlen : sizeof(command) - 1;

		error = SYSCTL_IN(req, command, length);
		if (error == 0) {
			command[length] = 0;
			answer[0] = 0;
			begonia_wifi_link(command, answer, sizeof(answer));
		}
		lck_mtx_unlock(&wifi_cmd_mtx);
		return error;
	}
	error = SYSCTL_OUT(req, answer, strlen(answer) + 1);
	lck_mtx_unlock(&wifi_cmd_mtx);
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_wifi, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    0, 0, sysctl_begonia_wifi, "A", "the Wi-Fi driver's commands and their answer");
