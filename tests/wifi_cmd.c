/* SPDX-License-Identifier: MIT
 * The Wi-Fi driver's commands through kern.begonia_wifi (kernel/bsd/dev/begonia_wifi_cmd.c), with nobody at the phone:
 *   wifi_cmd                 status, up, status, scan, sta (the bring-up and a scan, every answer printed)
 *   wifi_cmd COMMAND...      those, in order ("scan 2g", "join SSID PASSPHRASE" as one argument each)
 * Every answer is printed between "BEGONIA WIFI CMD: <command>" and "BEGONIA WIFI CMD: end".
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static void run(const char *command) {
    static char answer[8192];
    size_t size = sizeof(answer);

    printf("BEGONIA WIFI CMD: %s\n", command);
    if (sysctlbyname("kern.begonia_wifi", NULL, NULL, (void *)command, strlen(command)) != 0) {
        perror("BEGONIA WIFI CMD: the command");
        return;
    }
    if (sysctlbyname("kern.begonia_wifi", answer, &size, NULL, 0) != 0) {
        perror("BEGONIA WIFI CMD: the answer");
        return;
    }
    fputs(answer, stdout);
    printf("\nBEGONIA WIFI CMD: end\n");
}

int main(int argc, char **argv) {
    static const char *const sequence[] = {"status", "up", "status", "scan", "sta"};
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1) {
        for (int i = 1; i < argc; i++) run(argv[i]);
    } else {
        for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) run(sequence[i]);
    }
    return 0;
}
