/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>

#define BLOCKS_COUNT 4
#define BATTERY_WARNING_THRESHOLD 10
#define BATTERY_CRITICAL_THRESHOLD 7

char *tz = "Europe/London";

float prev_battery_level = 100;

static Display *dpy;

void notify(char *msg) {
	int msg_len = strlen(msg);
	char *command = (char *) malloc((9 + msg_len) * sizeof(char));
	if (!command) return;
	strcpy(command, "herbe ");
	strcat(command, msg);
	strcat(command, " &");
	system(command);
	free(command);
}

void denotify(void) {
    char *command = (char *) malloc(21 * sizeof(char));
    if (!command) return;
    strcpy(command, "pkill -SIGUSR1 herbe");
    system(command);
    free(command);
}

//https://stackoverflow.com/a/2736841
char *remove_ext(char* myStr, char extSep, char pathSep) {
    char *retStr, *lastExt, *lastPath;

    // Error checks and allocate string.
    if (myStr == NULL) return NULL;
    if ((retStr = malloc (strlen (myStr) + 1)) == NULL) return NULL;

    // Make a copy and find the relevant characters.
    strcpy(retStr, myStr);
    lastExt = strrchr (retStr, extSep);
    lastPath = (pathSep == 0) ? NULL : strrchr (retStr, pathSep);

    // If it has an extension separator.
    if (lastExt != NULL) {
        // and it's to the right of the path separator.
        if (lastPath != NULL) {
            if (lastPath < lastExt) {
                // then remove it.
                *lastExt = '\0';
            }
        } else {
            // Has extension separator with no path separator.
            *lastExt = '\0';
        }
    }

    return retStr;
}

char *smprintf(char *fmt, ...) {
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void settz(char *tzname) {
	setenv("TZ", tzname, 1);
}

char *mktimes(char *fmt) {
	char buf[129];
	time_t tim;
	struct tm *timtm;

	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void setstatus(char *str) {
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *readfile(char *base, char *file) {
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *getbattery(char *base) {
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return NULL;
	if (co[0] != '1') {
		free(co);
		return NULL;
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if (!strncmp(co, "Charging", 8)) {
		status = '+';
	} else if (!strncmp(co, "Full", 4)) {
		status = '=';
	} else {
		status = '?';
	}
    free(co);

	if (remcap < 0 || descap < 0)
		return smprintf("Invalid battery level");

	float level = ((float) remcap / (float) descap) * 100;
    level = level > 100 ? 100 : level;
    if (level == 100) {
        status = '=';
    }

	if (level <= BATTERY_WARNING_THRESHOLD && prev_battery_level > BATTERY_WARNING_THRESHOLD) {
		notify("\"Battery low\"");
	}
	if (level <= BATTERY_CRITICAL_THRESHOLD && prev_battery_level > BATTERY_CRITICAL_THRESHOLD) {
		notify("\"Battery critically low\"");
	}
	if (status == '+') {
        denotify();
	}
	prev_battery_level = level;

	return smprintf("%.0f%%%c", level, status);
}

char *readproc(char *cmd, int size) {
	FILE *p = popen(cmd, "r");
	char *out;
	size_t n = 0;
	int c;

	if (!p) return NULL; //could not open file

	out = (char *) malloc(size);

	while ((c = fgetc(p)) != EOF && n < size-1) {
		out[n++] = (char) c;
	}

	// don't forget to terminate with the null character
	if (n > 0) out[n-1] = '\0';
	else out[n] = '\0';

	pclose(p);

	return out;
}

char *getmpvfile() {
	char *file = readproc("/usr/bin/mpvctl get_file", 100);
	if (!file) return NULL;
    char *trimmed = remove_ext(file, '.', '/');
    char *ret = (strlen(trimmed) == 0) ? NULL : smprintf("%s", trimmed);
    free(file);
    free(trimmed);
    return ret;
}

char *getnetworkstatus(int show_ip) {
    char *ret;
	char *state = readproc("/usr/sbin/wpa_cli status | grep \"^wpa_state\" | cut -d'=' -f 2", 24);
	if (state == NULL) {
		ret = NULL;
	} else if (!strcmp(state, "COMPLETED")) {
		char *ssid = readproc("/usr/sbin/wpa_cli status | grep \"^ssid\" | cut -d'=' -f 2", 33);
		if (ssid == NULL || strlen(ssid) == 0) {
			ssid = smprintf("----");
		}
		if (show_ip) {
		    char *ip = readproc("/usr/sbin/wpa_cli status | grep \"^ip_address\" | cut -d'=' -f 2", 15);
		    if (ip == NULL || strlen(ip) == 0) {
			    ip = smprintf("---.---.---.---");
		    }
		    ret = smprintf("%s %s", ssid, ip);
            free(ip);
		}  else {
            ret = smprintf("%s", ssid);
		}
        free(ssid);
	} else if (!strcmp(state, "DISCONNECTED")) {
		ret = smprintf("Disconnected");
	} else if (!strcmp(state, "INTERFACE_DISABLED")) {
		ret = smprintf("Disabled");
	} else if (!strcmp(state, "SCANNING")) {
		ret = smprintf("Scanning");
	} else if (!strcmp(state, "ASSOCIATING")) {
		ret = smprintf("Associating");
	} else if (!strcmp(state, "4WAY_HANDSHAKE")) {
		ret = smprintf("Handshake");
	} else {
		ret = smprintf("%s", state);
	}
    free(state);
    return ret;
}

int main(void) {
    char *blocks[BLOCKS_COUNT];
	char *status;

	settz(tz);

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

    int blink = 0;

	for (;;sleep(1)) {
		blocks[0] = blink ? mktimes("%a %d %b %H %M") : mktimes("%a %d %b %H:%M");
		blocks[1] = getbattery("/sys/class/power_supply/BAT0");
		blocks[2] = getnetworkstatus(0);
        blocks[3] = getmpvfile();

	    status = "";
        for (int i = 0; i < BLOCKS_COUNT; i++) {
            char *block = blocks[i];
            if (block) {
                status = smprintf(" [%s]%s", block, status);
                free(block);
            }
        }
		setstatus(status);
		free(status);
        blink = !blink;
	}

	XCloseDisplay(dpy);

	return 0;
}

