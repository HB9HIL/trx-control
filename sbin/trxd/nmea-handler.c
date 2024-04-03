/*
 * Copyright (c) 2023 - 2024 Marc Balmer HB9SSB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Handle incoming NMEA data  */

#include <err.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "trxd.h"

extern int verbose;

#ifdef NMEA_DEBUG
#define DPRINTFN(n, x)	do { if (nmeadebug > (n)) printf x; } while (0)
int nmeadebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

#define NMEAMAX		82
#define LOCMAX		6
#define MAXFLDS		32
#define KNOTTOMS	(0.514444 )
#ifdef NMEA_DEBUG
#define TRUSTTIME	30
#else
#define TRUSTTIME	(10 * 60)	/* 10 minutes */
#endif

struct nmea {
	char			cbuf[NMEAMAX];	/* receive buffer */
	struct tm		tm;
	int			status;		/* signal status */
	double			latitude;
	double			longitude;
	double			altitude;
	double			speed;

	int			sync;		/* if 1, waiting for '$' */
	int			pos;		/* position in rcv buffer */
	char			mode;		/* GPS mode */
	char			locator[LOCMAX + 1];
};

/* NMEA decoding */
static void	nmea_scan(struct nmea *);
static void	nmea_gprmc(struct nmea *, char *fld[], int fldcnt);
static void	nmea_decode_gga(struct nmea *, char *fld[], int fldcnt);

/* Maidenhead Locator */
static int	nmea_locator(struct nmea *);

/* date and time conversion */
static int	nmea_date(char *s, struct tm *tm);
static int	nmea_time(char *s, struct tm *tm);

/* longitude and latitude conversion */
static int	nmea_degrees(double *dst, char *src, int neg);
static int	nmea_atoi(int64_t *dst, char *src);

static void
nmea_dump(struct nmea *np)
{
	printf("Date/time: %02d.%02d.%04d %02d:%02d:%02d\n",
	    np->tm.tm_mday, np->tm.tm_mon, np->tm.tm_year + 1900,
	    np->tm.tm_hour, np->tm.tm_min, np->tm.tm_sec);
	printf("Status   : %d\n", np->status);
	printf("Latitude : %8.4f\n", np->latitude);
	printf("Longitude: %8.4f\n", np->longitude);
	printf("Altitude : %4.2f m\n", np->altitude);
	printf("Speed    : %6.2f m/s\n", np->speed);
	printf("GPS mode : %c\n", np->mode);
	printf("Locator  : %s\n", np->locator);
	printf("\n");
}

/* Collect NMEA sentences from the device. */
static int
nmea_input(int c, struct nmea *np)
{	switch (c) {
	case '$':
		np->pos = np->sync = 0;
		break;
	case '\r':
	case '\n':
		if (!np->sync) {
			np->cbuf[np->pos] = '\0';
			nmea_scan(np);
			np->sync = 1;
		}
		break;
	default:
		if (!np->sync && np->pos < (NMEAMAX - 1))
			np->cbuf[np->pos++] = c;
		break;
	}
}

/* Scan the NMEA sentence just received. */
static void
nmea_scan(struct nmea *np)
{
	int fldcnt = 0, cksum = 0, msgcksum, n;
	char *fld[MAXFLDS], *cs;

	if (verbose)
		printf("%s\n", np->cbuf);

	/* split into fields and calculate the checksum */
	fld[fldcnt++] = &np->cbuf[0];	/* message type */
	for (cs = NULL, n = 0; n < np->pos && cs == NULL; n++) {
		switch (np->cbuf[n]) {
		case '*':
			np->cbuf[n] = '\0';
			cs = &np->cbuf[n + 1];
			break;
		case ',':
			if (fldcnt < MAXFLDS) {
				cksum ^= np->cbuf[n];
				np->cbuf[n] = '\0';
				fld[fldcnt++] = &np->cbuf[n + 1];
			} else {
				DPRINTF(("nr of fields in %s sentence exceeds "
				    "maximum of %d\n", fld[0], MAXFLDS));
				return;
			}
			break;
		default:
			cksum ^= np->cbuf[n];
		}
	}

	/*
	 * we only look at the messages coming from well-known sources or
	 * 'talkers', distinguished by the two-chars prefix, the most common
	 * being:
	 * GPS (GP)
	 * Glonass (GL)
	 * BeiDou (BD)
	 * Galileo (GA)
	 * 'Any kind/a mix of GNSS systems' (GN)
	 */
	if (strncmp(fld[0], "BD", 2) &&
	    strncmp(fld[0], "GA", 2) &&
	    strncmp(fld[0], "GL", 2) &&
	    strncmp(fld[0], "GN", 2) &&
	    strncmp(fld[0], "GP", 2))
		return;

	/* we look for the RMC & GGA messages */
	if (strncmp(fld[0] + 2, "RMC", 3) &&
	    strncmp(fld[0] + 2, "GGA", 3))
		return;

	/* if we have a checksum, verify it */
	if (cs != NULL) {
		msgcksum = 0;
		while (*cs) {
			if ((*cs >= '0' && *cs <= '9') ||
			    (*cs >= 'A' && *cs <= 'F')) {
				if (msgcksum)
					msgcksum <<= 4;
				if (*cs >= '0' && *cs<= '9')
					msgcksum += *cs - '0';
				else if (*cs >= 'A' && *cs <= 'F')
					msgcksum += 10 + *cs - 'A';
				cs++;
			} else {
				DPRINTF(("bad char %c in checksum\n", *cs));
				return;
			}
		}
		if (msgcksum != cksum) {
			DPRINTF(("checksum mismatch\n"));
			return;
		}
	}
	if (strncmp(fld[0] + 2, "RMC", 3) == 0)
		nmea_gprmc(np, fld, fldcnt);
	if (strncmp(fld[0] + 2, "GGA", 3) == 0)
		nmea_decode_gga(np, fld, fldcnt);
	nmea_locator(np);
	if (verbose)
		nmea_dump(np);
}

/* Decode the recommended minimum specific GPS/TRANSIT data. */
static void
nmea_gprmc(struct nmea *np, char *fld[], int fldcnt)
{
	if (fldcnt < 12 || fldcnt > 14) {
		DPRINTF(("gprmc: field count mismatch, %d\n", fldcnt));
		return;
	}
	if (nmea_time(fld[1], &np->tm)) {
		DPRINTF(("gprmc: illegal time, %s\n", fld[1]));
		return;
	}
	if (nmea_date(fld[9], &np->tm)) {
		DPRINTF(("gprmc: illegal date, %s\n", fld[9]));
		return;
	}

	if (*fld[12] != np->mode)
		np->mode = *fld[12];

	switch (*fld[2]) {
	case 'A':	/* The GPS has a fix */
	case 'D':
		np->status = 1;
		break;
	case 'V':	/*
			 * The GPS indicates a warning status, do not add to
			 * the timeout, if the condition persist, the sensor
			 * will be degraded.  Signal the condition through
			 * the signal sensor.
			 */
		np->status = 0;
		break;
	}
	if (nmea_degrees(&np->latitude, fld[3], *fld[4] == 'S' ? 1 : 0))
		;
	if (nmea_degrees(&np->longitude, fld[5], *fld[6] == 'W' ? 1 : 0))
		;

	/* convert from knot to m/s */
	np->speed = atof(fld[7]) * KNOTTOMS;
}

/* Decode the GPS fix data for altitude.
 * - field 9 is the altitude in meters
 * $GNGGA,085901.00,1234.5678,N,00987.12345,E,1,12,0.84,1040.9,M,47.4,M,,*4B
 */
static void
nmea_decode_gga(struct nmea *np, char *fld[], int fldcnt)
{
	if (fldcnt != 15) {
		DPRINTF(("GGA: field count mismatch, %d\n", fldcnt));
		return;
	}

	np->altitude = atof(fld[9]);
}

static int
nmea_locator(struct nmea *np)
{
	double lat, lon;

	if (np->longitude > 180.0 || np->longitude < -180.0
	   || np->latitude > 90.0 || np->latitude < -90.0)
		return -1;

	lon = np->longitude + 180.0;
	lat = np->latitude + 90.0;

	np->locator[0] = 'A' + lon / 20;
	np->locator[1] = 'A' + lat / 10;
	np->locator[2] = '0' + (int)lon % 20 / 2;
	np->locator[3] = '0' + (int)lat % 10;
	np->locator[4] = 'A' + (lon - (int)lon / 2 * 2 ) * 12;
	np->locator[5] = 'A' + (lat - (int)lat) * 24;

	np->locator[6] = '\0';

	return 0;
}

/*
 * Convert nmea integer/decimal values in the form of XXXX.Y to an integer value
 * if it's a meter/altitude value, will be returned as mm
 */
static int
nmea_atoi(int64_t *dst, char *src)
{
	char *p;
	int i = 3; /* take 3 digits */
	*dst = 0;

	for (p = src; *p && *p != '.' && *p >= '0' && *p <= '9' ; )
		*dst = *dst * 10 + (*p++ - '0');

	/* *p should be '.' at that point */
	if (*p != '.')
		return -1;	/* no decimal point, or bogus value ? */
	p++;

	/* read digits after decimal point, stop at first non-digit */
	for (; *p && i > 0 && *p >= '0' && *p <= '9' ; i--)
		*dst = *dst * 10 + (*p++ - '0');

	for (; i > 0 ; i--)
		*dst *= 10;

	DPRINTFN(2,("%s -> %lld\n", src, *dst));
	return 0;
}

/*
 * Convert a nmea position in the form DDDMM.MMMM to an
 * angle value (degrees*1000000)
 */
static int
nmea_degrees(double *dst, char *src, int neg)
{
	size_t ppos;
	int i, n;
	double deg = 0, min = 0, sec = 0;
	char *p;

	while (*src == '0')
		++src;	/* skip leading zeroes */

	for (p = src, ppos = 0; *p; ppos++)
		if (*p++ == '.')
			break;

	if (*p == '\0')
		return -1;	/* no decimal point */

	for (n = 0; *src && n + 2 < ppos; n++)
		deg = deg * 10 + (*src++ - '0');

	for (; *src && n < ppos; n++)
		min = min * 10 + (*src++ - '0');

	src++;	/* skip decimal point */

	for (; *src && n < (ppos + 4); n++)
		min = min * 10 + (*src++ - '0');

	deg = deg + (min / 600000);

	*dst = neg ? -deg : deg;
	return 0;
}

/*
 * Convert a NMEA 0183 formatted date string to seconds since the epoch.
 * The string must be of the form DDMMYY.
 * Return 0 on success, -1 if illegal characters are encountered.
 */
static int
nmea_date(char *s, struct tm *tm)
{
	char *p;
	int n;

	/* make sure the input contains only numbers and is six digits long */
	for (n = 0, p = s; n < 6 && *p && *p >= '0' && *p <= '9'; n++, p++)
		;
	if (n != 6 || (*p != '\0'))
		return -1;

	tm->tm_year = 100 + (s[4] - '0') * 10 + (s[5] - '0');
	tm->tm_mon = (s[2] - '0') * 10 + (s[3] - '0');
	tm->tm_mday = (s[0] - '0') * 10 + (s[1] - '0');

	return 0;
}

/*
 * Convert NMEA 0183 formatted time string to nanoseconds since midnight.
 * The string must be of the form HHMMSS[.[sss]] (e.g. 143724 or 143723.615).
 * Return 0 on success, -1 if illegal characters are encountered.
 */
static int
nmea_time(char *s, struct tm *tm)
{
	tm->tm_hour = (s[0] - '0') * 10 + (s[1] - '0');
	tm->tm_min = (s[2] - '0') * 10 + (s[3] - '0');
	tm->tm_sec = (s[4] - '0') * 10 + (s[5] - '0');

	/* Skip decimal fraction */
	return 0;
}

static void
cleanup(void *arg)
{
	nmea_tag_t *t = (nmea_tag_t *)arg;

	close(t->fd);
	free(t);
}

static void
cleanup_nmea(void *arg)
{
	struct nmea *np = (struct nmea *)arg;

	free(np);
}

void *
nmea_handler(void *arg)
{
	nmea_tag_t *t = (nmea_tag_t *)arg;
	struct nmea *np;
	struct pollfd pfd;
	int n, fd;
	char data;

	if (pthread_detach(pthread_self()))
		err(1, "nmea-handler: pthread_detach");

	pthread_cleanup_push(cleanup, arg);

	if (pthread_setname_np(pthread_self(), "nmea"))
		err(1, "nmea-handler: pthread_setname_np");

	np = malloc(sizeof(struct nmea));
	if (np == NULL)
		err(1, "nmea-handler: malloc");
	np->longitude = np->latitude = np->speed = np->altitude = 0;
	np->locator[0] = '\0';
	np->sync = 1;

	pthread_cleanup_push(cleanup_nmea, np);


	pfd.fd = t->fd;
	pfd.events = POLLIN;

	for (;;) {
		if (poll(&pfd, 1, 0) == -1)
			err(1, "nmea-handler: poll");

		if (pfd.revents) {
			read(t->fd, &data, 1);
			nmea_input(data, np);
		}
	};
	pthread_cleanup_pop(0);
	pthread_cleanup_pop(0);
	return NULL;
}
