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
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>

#define BATTERY_WARNING_THRESHOLD 10
#define BATTERY_CRITICAL_THRESHOLD 7

char *tzlondon = "Europe/London";

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

char *mktimes(char *fmt, char *tzname) {
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
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

char *loadavg3(void) {
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *loadavg(void) {
	double avgs[2];

	if (getloadavg(avgs, 2) < 0)
		return smprintf("");

    char direction = (avgs[0] > avgs[1]) ? '+' : ((avgs[0] < avgs[1]) ? '-' : L'=');

	return smprintf("%.2f%c", avgs[0], direction);
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
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
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

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	float level = ((float) remcap / (float) descap) * 100;

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

char *gettemperature(char *base, char *sensor) {
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

char *readproc(char *cmd, int size, int stripfinal) {
	FILE *p = popen(cmd, "r");
	char *out;
	size_t n = 0;
	int c;

	if (p == NULL) {
		return NULL; //could not open file
	}

	out = malloc(size);

	while ((c = fgetc(p)) != EOF) {
		out[n++] = (char) c;
	}

	// don't forget to terminate with the null character
	if (stripfinal && n > 0) {
		out[n-1] = '\0';
	} else {
		out[n] = '\0';
	}

	pclose(p);

	return out;
}

char *getvolume() {
	char *out = readproc("/usr/local/bin/pamixer --get-volume-human", 6, 1);
	if (out == NULL) {
		out = "---%";
	}
	return smprintf("%s", out);
}

char *getnetworkstatus(int show_ip) {
	char *state = readproc("/usr/sbin/wpa_cli status | grep \"^wpa_state\" | cut -d'=' -f 2", 18, 1);
	if (state == NULL) {
		return smprintf("Unknown");
	} else if (!strcmp(state, "COMPLETED")) {
		char *ssid = readproc("wpa_cli status | grep \"^ssid\" | cut -d'=' -f 2", 33, 1);
		if (ssid == NULL || strlen(ssid) == 0) {
			ssid = "----";
		}
		if (show_ip) {
		    char *ip = readproc("wpa_cli status | grep \"^ip_address\" | cut -d'=' -f 2", 15, 1);
		    if (ip == NULL || strlen(ip) == 0) {
			    ip = "---.---.---.---";
		    }
		    return smprintf("%s %s", ssid, ip);
		}  else {
            return smprintf("%s", ssid);
		}
	} else if (!strcmp(state, "DISCONNECTED")) {
		return smprintf("Disconnected");
	} else if (!strcmp(state, "INTERFACE_DISABLED")) {
		return smprintf("Disabled");
	} else if (!strcmp(state, "SCANNING")) {
		return smprintf("Scanning");
	} else if (!strcmp(state, "ASSOCIATING")) {
		return smprintf("Associating");
	} else if (!strcmp(state, "4WAY_HANDSHAKE")) {
		return smprintf("Handshake");
	} else {
		return smprintf(state);
	}
}

int main(void) {
	char *status;
	char *network;
	char *avgs;
	char *bat;
	char *vol;
	char *tmldn;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
		network = getnetworkstatus(0);
		avgs = loadavg();
		bat = getbattery("/sys/class/power_supply/BAT0");
		vol = getvolume();
		tmldn = mktimes("%a %d %b %H:%M:%S", tzlondon);

		status = smprintf(" [%s] [%s] [%s] [%s] [%s]",
				network, avgs, bat, vol, tmldn);
		setstatus(status);
		free(network);
		free(avgs);
		free(bat);
		free(vol);
		free(tmldn);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

