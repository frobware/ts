// Copyright (C) 2023, 2024, Andrew McDermott. All rights reserved.

// This file is part of the https://github.com/frobware/ts project.
// For the full copyright and license information, please view the
// LICENSE file that was distributed with this source code.

// Feature test macro to enable clock_gettime.
#define _POSIX_C_SOURCE 200809L

// Feature test macro to enable strptime.
#define _XOPEN_SOURCE

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NELEMENTS(A)  (sizeof(A) / sizeof((A)[0]))

// MIN_TIME_BUFSZ - The minimum buffer size for formatting
// relative time differences.
//
// Calculation for the initial size of 136 characters:
// - Assumes time_t can have a maximum of 19 digits.
//
// - TIME_UNIT_COUNT is 6, representing years, months, days, hours,
//   minutes, seconds.
//
// - Allocates 2 additional characters per unit for the symbol and a
//   separator, adding 12 characters in total.
//
// - Accounts for a maximum of 9 characters for the direction strings
//   (i.e., " from now" and " ago").
//
// - Includes 1 character for the null terminator.
//
// This results in an initial buffer size calculation of (6 * (19 +
// 2)) + 9 + 1 = 136 characters.
//
// The size is then rounded up to the next power of 2, which is 256,
// to potentially benefit from memory alignment optimizations. This
// approach acknowledges that the buffer size is way larger than
// strictly necessary but prefers uniformity and simplicity,
// considering the buffer size remains relatively small and efficient.
#define MIN_TIME_BUFSZ 256

#ifndef MAX_TIME_BUFSZ
#define MAX_TIME_BUFSZ 4096
#endif

#define COMP_TIME_INIT(COMP_TIME, Y, D, H, M, S)	\
	do {						\
		(COMP_TIME)[YEAR_UNIT] = (Y);		\
		(COMP_TIME)[DAY_UNIT] = (D);		\
		(COMP_TIME)[HOUR_UNIT] = (H);		\
		(COMP_TIME)[MINUTE_UNIT] = (M);		\
		(COMP_TIME)[SECOND_UNIT] = (S);		\
	} while(0)

#define COMP_TIME_ASSERT(COMP_TIME, Y, D, H, M, S)		\
	do {							\
		assert((COMP_TIME)[YEAR_UNIT] == (Y));		\
		assert((COMP_TIME)[DAY_UNIT] == (D));		\
		assert((COMP_TIME)[HOUR_UNIT] == (H));		\
		assert((COMP_TIME)[MINUTE_UNIT] == (M));	\
		assert((COMP_TIME)[SECOND_UNIT] == (S));	\
	} while (0)

enum {
	YEAR_UNIT,
	DAY_UNIT,
	HOUR_UNIT,
	MINUTE_UNIT,
	SECOND_UNIT,
	TIME_UNIT_COUNT
};

enum sanitise_time_format_op {
	COLLAPSE_MICROSECOND_SPECFIERS = 1,
	EXPAND_MICROSECOND_SPECIFIERS,
};

struct ts_fmt {
	struct ts_opt *opt;
	char *sanitised_time_format;
	size_t n_microseconds_specifiers;
	char *buf;
	size_t bufsz;
};

struct ts_opt {
	bool flag_inc;
	bool flag_mono;
	bool flag_rel;
	bool flag_sincestart;
	bool hires_timestamping;
	bool user_format_specified;
	const char *format;
	int flag_precision;
};

// Which zone, if any, the matched text carries. ZONE_LOCAL is zero so
// that a pattern without a zone needs no annotation.
enum timestamp_zone {
	ZONE_LOCAL,
	ZONE_UTC,
	ZONE_NUMERIC,
};

struct timestamp_pattern {
	const char *const re;
	const char *const description;
	const char *strptime_format;
	enum timestamp_zone zone;
	pcre2_code *pcre;
	pcre2_match_data *match_data;
};

typedef time_t composite_time[TIME_UNIT_COUNT];

static struct timestamp_pattern timestamps[] = {{
		.re = "\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?Z",
		.description = "ISO-8601 with a Z suffix, as Kubernetes pod logs use",
		.strptime_format = "%Y-%m-%dT%H:%M:%S",
		.zone = ZONE_UTC,
	}, {
		.re = "\\d{2}\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{6}",
		.description = "Kubernetes client-go log format with microseconds",
		.strptime_format = "%m%d %H:%M:%S",
	}, {
		.re = "\\d+\\s+\\w\\w\\w\\s+\\d\\d+\\s+\\d\\d:\\d\\d:\\d\\d\\s+[+-]\\d\\d\\d\\d",
		.description = "16 Jun 94 07:29:35 with timezone",
		.strptime_format = "%d %b %y %H:%M:%S",
		.zone = ZONE_NUMERIC,
	}, {
		.re = "\\d\\d[-\\s\\/]\\w\\w\\w\\/\\d\\d+\\s+\\d\\d:\\d\\d:\\d\\d\\s+[+-]\\d\\d\\d\\d",
		.description = "21 dec/93 17:05:30 +0000",
		.strptime_format = "%d %b/%y %H:%M:%S",
		.zone = ZONE_NUMERIC,
	}, {
		.re = "\\d\\d[-\\s\\/]\\w\\w\\w\\s+\\d\\d:\\d\\d:\\d\\d\\s+[+-]\\d\\d\\d\\d",
		.description = "21 dec 17:05:30 +0000",
		.strptime_format = "%d %b %H:%M:%S",
		.zone = ZONE_NUMERIC,
	}, {
		.re = "\\d\\d[-\\s\\/]\\w\\w\\w\\/\\d\\d+\\s+\\d\\d:\\d\\d",
		.description = "21 dec/93 17:05 without seconds and timezone",
		.strptime_format = "%d %b/%y %H:%M",
	}, {
		.re = "\\d\\d[-\\s\\/]\\w\\w\\w\\s+\\d\\d:\\d\\d",
		.description = "21 dec 17:05 without seconds and timezone",
		.strptime_format = "%d %b %H:%M",
	}, {
		.re = "\\d\\d\\d\\d[-:]\\d\\d[-:]\\d\\dT\\d\\d:\\d\\d:\\d\\d(?:\\.\\d+)?",
		.description = "ISO-8601 format",
		.strptime_format = "%Y-%m-%dT%H:%M:%S",
	}, {
		.re = "\\w\\w\\w\\s+\\w\\w\\w\\s+\\d\\d\\s+\\d\\d:\\d\\d",
		.description = "Lastlog format",
		.strptime_format = "%a %b %d %H:%M",
	}, {
		.re = "\\w{3}\\s+\\d{1,2}\\s+\\d\\d:\\d\\d:\\d\\d",
		.description = "Syslog format with day",
		.strptime_format = "%b %d %H:%M:%S",
	},
};

static const int DAYS_PER_YEAR = 365;
static const int HOURS_PER_DAY = 24;
static const int MINUTES_PER_HOUR = 60;
static const int SECONDS_PER_MINUTE = 60;
static const int NANOSECONDS_PER_SECOND = 1000000000;

static const time_t SECONDS_PER_YEAR = DAYS_PER_YEAR * HOURS_PER_DAY * MINUTES_PER_HOUR * SECONDS_PER_MINUTE;
static const time_t SECONDS_PER_DAY = HOURS_PER_DAY * MINUTES_PER_HOUR * SECONDS_PER_MINUTE;
static const time_t SECONDS_PER_HOUR = MINUTES_PER_HOUR * SECONDS_PER_MINUTE;

static const int MAX_VALUES[TIME_UNIT_COUNT] = {
	[YEAR_UNIT] = INT_MAX,
	[DAY_UNIT] = DAYS_PER_YEAR,
	[HOUR_UNIT] = HOURS_PER_DAY,
	[MINUTE_UNIT] = MINUTES_PER_HOUR,
	[SECOND_UNIT] = SECONDS_PER_MINUTE
};

static const char *time_unit_symbol(int index)
{
	switch (index) {
	case YEAR_UNIT:
		return "year";
	case DAY_UNIT:
		return "day";
	case HOUR_UNIT:
		return "hour";
	case MINUTE_UNIT:
		return "minute";
	case SECOND_UNIT:
		return "second";
	default:
		return "unknown";
	}
}

static void seconds_to_composite_time(time_t seconds, composite_time comp_time)
{
	time_t remainder = seconds;

	comp_time[YEAR_UNIT] = remainder / SECONDS_PER_YEAR;
	remainder %= SECONDS_PER_YEAR;

	comp_time[DAY_UNIT] = remainder / SECONDS_PER_DAY;
	remainder %= SECONDS_PER_DAY;

	comp_time[HOUR_UNIT] = remainder / SECONDS_PER_HOUR;
	remainder %= SECONDS_PER_HOUR;

	comp_time[MINUTE_UNIT] = remainder / SECONDS_PER_MINUTE;
	remainder %= SECONDS_PER_MINUTE;

	comp_time[SECOND_UNIT] = remainder;
}

static time_t composite_time_to_seconds(composite_time comp_time)
{
	time_t total = 0;

	total += comp_time[YEAR_UNIT] * SECONDS_PER_YEAR;
	total += comp_time[DAY_UNIT] * SECONDS_PER_DAY;
	total += comp_time[HOUR_UNIT] * SECONDS_PER_HOUR;
	total += comp_time[MINUTE_UNIT] * SECONDS_PER_MINUTE;
	total += comp_time[SECOND_UNIT];

	return total;
}

// approximate_comp_time: Normalises time units to a set precision.
//
// Modifies an array of time_unit structs, ensuring no unit exceeds
// its max value while retaining a specified count of non-zero units.
// The units are ordered from largest to smallest (e.g., years to
// seconds). The first unit (usually years) is treated specially and
// is never considered improper or over any limit. Rounds up the next
// significant unit if at least half its max value, resetting the
// current and less significant units to 0. Also handles cases where a
// unit's count equals or exceeds its max value by resetting it to 0
// and incrementing the previous unit. Repeats until non-zero units
// meet precision or all units are proper.
//
// The first unit is never reset or considered "too many" non-zeros,
// as it is the most significant.
//
// Params:
//   - units: Array of time_unit structs from largest to smallest.
//   - unitCount: Total units in the array.
//   - precision: Max non-zero units to keep.
//
// Precision Behaviour:
//   - 0: Resets all units to 0, nullifying duration.
//   - 1: Retains only the most significant non-zero unit, rounding
//        up as needed. The first unit (e.g., years) is always
//        retained regardless of its value.
//   - N (2 to `unitCount`-1): Keeps N most significant non-zero units,
//        rounding (N+1)th if needed.
//   - >= `unitCount`: No discarding or rounding, full detail kept.
//
// Operation:
//   1. Loop through units array.
//   2. Count non-zero units, skipping the first unit.
//   3. If non-zero units exceed precision:
//      - Increment previous unit if current is at least half its max.
//      - Reset current and subsequent units to 0.
//      - Restart approximation.
//   4. If unit count equals/exceeds max (improper), increment previous,
//      reset current to 0, restart approximation.
//   5. Continue until units are proper and precision is met.
//
// Example Usage:
//   - Given units: 0y, 0h, 1m, 2s.
//   - Precision = 1: Results in 0y, 1m (0h, 0s).
//   - Precision = 2: Results in 0y, 1m, 2s (0h).
static void approximate_time(int precision, composite_time comp_time)
{
	int overflowing_index;
	int non_zero_count;

reapproximate:
	overflowing_index = -1;
	non_zero_count = 0;

	for (size_t i = 0; i < TIME_UNIT_COUNT; i++) {
		if (comp_time[i] == 0)
			continue;

		non_zero_count++;

		if (i == YEAR_UNIT) {
			// Years never overflow.
			continue;
		}

		if (non_zero_count > precision) {
			if (comp_time[i] >= MAX_VALUES[i] / 2) {
				comp_time[i - 1]++;
			}
			// Reset this and subsequent values to 0.
			for (size_t j = i; j < TIME_UNIT_COUNT; j++) {
				comp_time[j] = 0;
			}
			goto reapproximate;
		} else if (comp_time[i] >= MAX_VALUES[i]) {
			overflowing_index = i;
		}
	}

	if (overflowing_index != -1) {
		// Adjust one overflowing time unit per iteration.
		comp_time[overflowing_index - 1]++;
		comp_time[overflowing_index] = 0;
		goto reapproximate;
	}
}

// Checks if a placeholder (%.S, %.s, or %.T) is found at the current
// position in format.
static bool is_microsecond_placeholder(const char *format, size_t format_length, size_t pos, char *spec) {
	if (pos + 2 < format_length &&
	    format[pos] == '%' && format[pos + 1] == '.' &&
	    (format[pos + 2] == 'S' || format[pos + 2] == 's' || format[pos + 2] == 'T')) {
		if (spec != NULL)
			*spec = format[pos + 2];
		return true;
	}
	return false;
}

static size_t count_microsecond_specifiers(const char *format)
{
	size_t count = 0;
	size_t format_len = strlen(format);

	for (size_t i = 0; i < format_len; i++) {
		if (is_microsecond_placeholder(format, format_len, i, NULL)) {
			count++;
			i += 2;
		}
	}

	return count;
}

static int sanitise_time_format(const char *format,
				char **pbuf,
				size_t *n_microsecond_specifiers,
				enum sanitise_time_format_op op)
{
	*n_microsecond_specifiers = count_microsecond_specifiers(format);

	size_t format_length = strlen(format);
	size_t required_capacity = format_length + 1;

	if (op == EXPAND_MICROSECOND_SPECIFIERS) {
		required_capacity += *n_microsecond_specifiers * 6;
	}

	if ((*pbuf = (char *)calloc(required_capacity, 1)) == NULL) {
		return -1;
	}

	for (size_t i = 0, wr = 0; i < format_length; i++) {
		char specifier;
		if (is_microsecond_placeholder(format, format_length, i, &specifier)) {
			(*pbuf)[wr++] = '%';
			(*pbuf)[wr++] = specifier;
			if (op == EXPAND_MICROSECOND_SPECIFIERS) {
				memcpy(*pbuf + wr, ".000000", 7);
				wr += 7;
			}
			i += 2;	// i++ in the loop corrects to 3.
		} else {
			(*pbuf)[wr++] = format[i];
		}
	}

	return 0;
}

static int validate_time_format(const char *format, char **pbuf, size_t *bufsz)
{
	struct tm time_info = { 0 };
	char *buf = NULL;

	*bufsz = MIN_TIME_BUFSZ;

	while (*bufsz <= MAX_TIME_BUFSZ) {
		size_t n;
		char *new_buf;
		if ((new_buf = realloc(buf, *bufsz)) == NULL) {
			free(buf);
			return -1;
		}
		buf = new_buf;
		n = strftime(buf, *bufsz, format, &time_info);
		if (n > 0) {
			*pbuf = buf;
			return 0;
		} else if (n == 0 && *bufsz < MAX_TIME_BUFSZ) {
			*bufsz = (*bufsz < MAX_TIME_BUFSZ / 2)
				? *bufsz * 2
				: MAX_TIME_BUFSZ;
		} else {
			// Reached the permissible maximum or strftime
			// legitimately returned 0 (empty string).
			*pbuf = buf;
			return (n == 0 && *bufsz == MAX_TIME_BUFSZ) ? 0 : -1;
		}
	}

	if (buf != NULL)
		free(buf);

	return -1;
}

static size_t write_ull_padded(char *buf, size_t offset, unsigned long long value, size_t width)
{
	unsigned long long temp = value;
	int ndigits = (temp == 0) ? 1 : 0;

	while (temp > 0) {
		ndigits++;
		temp /= 10;
	}

	int required_padding = width - ndigits;
	if (required_padding < 0 || width == 0) {
		required_padding = 0;
	}

	for (int i = 0; i < required_padding; i++) {
		buf[offset+i] = '0';
	}

	temp = value;

	for (int i = ndigits - 1; i >= 0; i--) {
		buf[offset + i] = (temp % 10) + '0';
		temp /= 10;
	}

	return ndigits + required_padding;
}

static void format_comp_time(char *buf, const composite_time comp_time, const char *const direction, size_t direction_len)
{
	size_t offset = 0;

	for (size_t i = 0; i < TIME_UNIT_COUNT; i++) {
		if (comp_time[i] > 0) {
			offset += write_ull_padded(buf, offset, comp_time[i], 0);
			buf[offset++] = *time_unit_symbol(i);
		}
	}

	for (size_t i = 0; i < direction_len; i++) {
		buf[offset++] = direction[i];
	}

	buf[offset] = '\0';
}

// days_from_civil - Days since 1970-01-01 for a proleptic Gregorian
// date. Howard Hinnant's algorithm, valid across the whole time_t
// range rather than just the epoch onwards.
static time_t days_from_civil(int year, int month, int day)
{
	year -= month <= 2;

	const int era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yoe = (unsigned)(year - era * 400);
	const unsigned doy = (153 * (unsigned)(month + (month > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return (time_t)era * 146097 + (time_t)doe - 719468;
}

// tm_to_utc_seconds - Read TM as a civil time in UTC and return the
// seconds since the epoch. Unlike mktime this consults neither TZ nor
// tm_isdst nor tm_gmtoff, so every platform agrees on the answer.
static time_t tm_to_utc_seconds(const struct tm *tm)
{
	const time_t days = days_from_civil(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

	return days * SECONDS_PER_DAY
		+ tm->tm_hour * SECONDS_PER_HOUR
		+ tm->tm_min * SECONDS_PER_MINUTE
		+ tm->tm_sec;
}

// parse_utc_offset - Read the +hhmm or -hhmm suffix that the
// numeric-zone patterns end with, returning seconds east of UTC.
// strptime cannot be trusted for this: glibc records the offset in
// tm_gmtoff and leaves the fields alone, whereas BSD rewrites the
// fields into the local zone and reports the local offset instead.
static long parse_utc_offset(const char *text, size_t len)
{
	if (len < 5) {
		return 0;
	}

	const char *sign = text + len - 5;

	if (*sign != '+' && *sign != '-') {
		return 0;
	}

	for (size_t i = 1; i < 5; i++) {
		if (sign[i] < '0' || sign[i] > '9') {
			return 0;
		}
	}

	const long hours = (sign[1] - '0') * 10 + (sign[2] - '0');
	const long minutes = (sign[3] - '0') * 10 + (sign[4] - '0');
	const long offset = hours * SECONDS_PER_HOUR + minutes * SECONDS_PER_MINUTE;

	return *sign == '-' ? -offset : offset;
}

static const struct timestamp_pattern *match_timestamp(char *subject, ssize_t len, size_t *match_start, size_t *match_end)
{
	*match_start = *match_end = 0;

	for (size_t i = 0; i < NELEMENTS(timestamps); i++) {
                if (pcre2_match(timestamps[i].pcre, (PCRE2_SPTR)subject, len, 0, 0, timestamps[i].match_data, NULL) < 0)
			continue; // No match.
		size_t *ovector = pcre2_get_ovector_pointer(timestamps[i].match_data);
		assert(ovector);
		*match_start = ovector[0];
		*match_end = ovector[1];
		return &timestamps[i];
	}

	return NULL;
}

// timestamp_to_epoch - Convert PARSED, the fields strptime read out of
// TEXT, into seconds since the epoch, honouring whichever zone PATTERN
// says the text carries. Only a zone-less timestamp goes through
// mktime, and only that case depends on TZ.
static time_t timestamp_to_epoch(const struct timestamp_pattern *pattern,
				 struct tm *parsed,
				 const char *text,
				 size_t len)
{
	switch (pattern->zone) {
	case ZONE_UTC:
		return tm_to_utc_seconds(parsed);
	case ZONE_NUMERIC:
		return tm_to_utc_seconds(parsed) - parse_utc_offset(text, len);
	case ZONE_LOCAL:
		break;
	}

	// Let mktime() determine DST.
	parsed->tm_isdst = -1;

	return mktime(parsed);
}

// Calculates a timestamp based on various modes and flags. This
// function handles both high-resolution (hires) and
// non-high-resolution (non-hires) timestamping.
//
// In high-resolution mode, it accounts for nanoseconds and adjusts
// the timestamp based on the monotonic clock offset (monodelta) if
// required. It also normalises nanoseconds to ensure they are within
// the standard range.
//
// In incremental mode (flag_inc), it calculates the delta (time
// difference) since the last timestamp and updates last_seconds and
// last_nanoseconds for the next calculation. This delta calculation
// is crucial for the correct functioning of the incremental
// timestamping.
//
// The flag_sincestart is used to calculate the time elapsed since the
// start of the program, providing timestamps relative to the
// program's start time rather than real-world time.
//
// The function maintains separate logic for hires and non-hires modes
// due to the different handling of nanoseconds.
//
// @param last_seconds       Pointer to the variable holding the seconds part
//                           of the last timestamp.
// @param last_nanoseconds   Pointer to the variable holding the nanoseconds
//                           part of the last timestamp.
// @param flag_mono          Indicates if the monotonic clock is used.
// @param flag_inc           Indicates if incremental mode is active.
// @param flag_sincestart    Indicates if the timestamp should be calculated
//                           since the start of the program.
// @param monodelta          The offset to be added in monotonic mode to align
//                           with real-world time.
// @param hires_timestamping Indicates if high-resolution timestamping is
//                           used.
// @return                   A timespec struct representing the calculated
//                           timestamp.
static bool gettime(const struct ts_opt *const ts, struct timespec *now, long *last_seconds, long *last_nanoseconds, long monodelta)
{
	if (clock_gettime(ts->flag_mono ? CLOCK_MONOTONIC : CLOCK_REALTIME, now) != 0)
		return false;

	if (ts->hires_timestamping) {
		if (ts->flag_mono) {
			now->tv_sec += monodelta;
		}

		if (now->tv_nsec >= NANOSECONDS_PER_SECOND) {
			now->tv_sec++;
			now->tv_nsec -= NANOSECONDS_PER_SECOND;
		}
	}

	if (ts->flag_inc || ts->flag_sincestart) {
		long delta_seconds = now->tv_sec - *last_seconds;
		long delta_nanoseconds = ts->hires_timestamping ? now->tv_nsec - *last_nanoseconds : 0;
		if (ts->hires_timestamping && delta_nanoseconds < 0) {
			delta_seconds--;
			delta_nanoseconds += NANOSECONDS_PER_SECOND;
		}

		if (ts->flag_inc) {
			*last_seconds = now->tv_sec;
			*last_nanoseconds = ts->hires_timestamping ? now->tv_nsec : 0;
		}

		now->tv_sec = delta_seconds;
		now->tv_nsec = delta_nanoseconds;
	}

	return true;
}

static void fmt_time_rel(struct ts_fmt *fmt, char *line, ssize_t line_len, size_t *match_end, struct timespec now)
{
	size_t match_start;

	*match_end = 0;
	fmt->buf[0] = '\0';

	const struct timestamp_pattern *pattern = match_timestamp(line, line_len, &match_start, match_end);

	if (pattern == NULL) {
		return;
	}

	// Isolate the timestamp within the line before parsing.
	char old_char = line[*match_end];
	line[*match_end] = '\0';

	// strptime only sets the fields in struct tm that correspond
	// to the components it finds in the input string based on the
	// provided format string. This means not all fields in struct
	// tm might be set by strptime if they're not represented in
	// the input string.
	struct tm parsed_tm = { 0 };

	if (strptime(&line[match_start], pattern->strptime_format, &parsed_tm) == NULL) {
		line[*match_end] = old_char;
		return;
	}

	line[*match_end] = old_char;

	if (parsed_tm.tm_year == 0) {
		struct tm *current_tm = localtime(&now.tv_sec);
		parsed_tm.tm_year = current_tm->tm_year;
	}

	// Convert the parsed timestamp to time_t to assess its
	// chronological relationship with the current time. In the
	// context of analyzing historic logs, it's essential to
	// verify that timestamps reflect past events. This step helps
	// identify and correct situations where the parsed timestamp,
	// due to format ambiguities or incomplete data (e.g., missing
	// year information), might erroneously be interpreted as
	// being in the future.

	const char *matched = &line[match_start];
	const size_t matched_len = *match_end - match_start;

	time_t parsed_time_t = timestamp_to_epoch(pattern, &parsed_tm, matched, matched_len);
	if (parsed_time_t > now.tv_sec) {
		parsed_tm.tm_year--;
		parsed_time_t = timestamp_to_epoch(pattern, &parsed_tm, matched, matched_len);
	}

	if (fmt->opt->user_format_specified) {
		// Render the instant in the local zone rather than
		// echoing back the fields as written, so that TZ
		// governs the output whatever zone the input carried.
		strftime(fmt->buf, fmt->bufsz, fmt->sanitised_time_format, localtime(&parsed_time_t));
	} else {
		time_t seconds_diff = difftime(now.tv_sec, parsed_time_t);

		if (seconds_diff == 0) {
			snprintf(fmt->buf, fmt->bufsz, "right now");
			return;
		}

		composite_time comp_time;

		seconds_to_composite_time(labs(seconds_diff), comp_time);
		approximate_time(fmt->opt->flag_precision, comp_time);
		format_comp_time(fmt->buf,
				 comp_time,
				 seconds_diff >= 0 ? " ago" : " from now",
				 seconds_diff >= 0 ? 4 : 9);
	}
}

static void fmt_time_now(struct ts_fmt *fmt, struct timespec now)
{
	*fmt->buf = '\0';

	size_t n = strftime(fmt->buf, fmt->bufsz, fmt->sanitised_time_format, localtime(&now.tv_sec));

	for (size_t i = 0; n > 0 && i < fmt->n_microseconds_specifiers; i++) {
		char *placeholder = strstr(fmt->buf, ".000000");
		if (placeholder != NULL) {
			write_ull_padded(placeholder, 1, now.tv_nsec / 1000, 6);
		}
	}
}

static void must_init_timestamp_patterns(void)
{
	for (size_t i = 0; i < NELEMENTS(timestamps); i++) {
		PCRE2_SIZE offset;
		PCRE2_SPTR pattern = (PCRE2_SPTR)timestamps[i].re;
		int rc;
		uint32_t options = PCRE2_UTF | PCRE2_UCP;

		timestamps[i].pcre = pcre2_compile(
			pattern,                // the pattern
			PCRE2_ZERO_TERMINATED,  // indicates the pattern is zero-terminated
			options,		// options
			&rc,			// for error number
			&offset,		// for error offset
			NULL                    // use default compile context
			);

		if (timestamps[i].pcre == NULL) {
			PCRE2_UCHAR buf[256];
			pcre2_get_error_message(rc, buf, sizeof(buf));
			fprintf(stderr, "PCRE2 compilation error for pattern#%zd: '%s', error='%s', offset=%ld.\n", i, timestamps[i].re, buf, offset);
			exit(EXIT_FAILURE);
		}

		timestamps[i].match_data = pcre2_match_data_create_from_pattern(timestamps[i].pcre, NULL);
		if (timestamps[i].match_data == NULL) {
			fprintf(stderr, "Failed to create match data for pattern %zu\n", i);
			exit(EXIT_FAILURE);
		}
	}
}

static bool init_clocks(const struct ts_opt *const ts, long *last_seconds, long *last_nanoseconds, long *monodelta)
{
	struct timespec now;

	if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
		return false;
	}

	*last_seconds = now.tv_sec;
	*last_nanoseconds = ts->hires_timestamping ? now.tv_nsec : 0;
	*monodelta = 0;

	if (ts->flag_mono) {
		struct timespec real_time;
		if (clock_gettime(CLOCK_MONOTONIC, &real_time) != 0) {
			return false;
		}

		if (now.tv_sec >= real_time.tv_sec) {
			*monodelta = now.tv_sec - real_time.tv_sec;
			*last_seconds = real_time.tv_sec + *monodelta;
			*last_nanoseconds = real_time.tv_nsec;
		} else {
			fprintf(stderr, "fatal error: real time is less than monotonic time!\n");
			exit(EXIT_FAILURE);
		}
	}

	return true;
}

// resolve_modes - Reconcile the mutually exclusive mode flags in OPT.
//
// -r converts timestamps already present in the input, which needs
// the wall clock. -i and -s replace the wall clock with an elapsed
// delta. Both cannot apply at once, and -r wins: moreutils ignores
// -i and -s on its relative path, so we do too.
static void resolve_modes(struct ts_opt *opt)
{
	if (opt->flag_rel) {
		opt->flag_inc = false;
		opt->flag_sincestart = false;
	}
}

// resolve_format - Return the strftime format OPT should use.
// USER_FORMAT is the format given on the command line, or NULL when
// none was. Call after resolve_modes.
static const char *resolve_format(const struct ts_opt *opt, const char *user_format)
{
	if (user_format != NULL) {
		return user_format;
	}

	if (opt->flag_inc || opt->flag_sincestart) {
		return "%H:%M:%S";
	}

	/*
	 * %b = Abbreviated month name
	 * %d = The day of the month as a decimal number
	 * %H = Hours
	 * %M = Minutes
	 * %S = Seconds
	 */
	return "%b %d %H:%M:%S";
}

static struct ts_opt parse_options(int argc, char *argv[])
{
	struct ts_opt option = { 0 };

	int opt;
	char *value_endptr;
	long value;

	option.flag_precision = 2; /* default */

	while ((opt = getopt(argc, argv, "imrsp:")) != -1) {
		switch (opt) {
		case 'i':
			option.flag_inc = true;
			break;
		case 'm':
			option.flag_mono = true;
			break;
		case 'r':
			option.flag_rel = true;
			break;
		case 's':
			option.flag_sincestart = true;
			break;
		case 'p':
			value = strtol(optarg, &value_endptr, 10);
			if ((errno == ERANGE && (value == LONG_MAX || value == LONG_MIN)) || (errno != 0 && value == 0)) {
				fprintf(stderr, "Error: -p %ld: %s.\n", value, strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (value_endptr == optarg || *value_endptr != '\0' ||
			    value < 1 || value >= TIME_UNIT_COUNT) {
				fprintf(stderr, "Error: -p %ld is out of range. Valid values are between 1 and %d inclusive.\n", value, TIME_UNIT_COUNT-1);
				exit(EXIT_FAILURE);
			}
			option.flag_precision = value;
			break;
		default:
			fprintf(stderr, "Usage: ts [-r] [-i | -s] [-m] [-p precision] [format]\n");
			exit(EXIT_FAILURE);
		}
	}

	// Resolve before the conflict check below: under -r the
	// incremental flags are ignored, so they cannot conflict.
	resolve_modes(&option);

	if (option.flag_inc && option.flag_sincestart) {
		fprintf(stderr, "Options '-i' and '-s' cannot be used together.\n");
		exit(EXIT_FAILURE);
	}

	const char *user_format = optind < argc ? argv[optind] : NULL;

	option.format = resolve_format(&option, user_format);
	option.user_format_specified = user_format != NULL;
	option.hires_timestamping = count_microsecond_specifiers(option.format) > 0 || option.flag_mono;

	if (option.flag_inc || option.flag_sincestart) {
		// An elapsed time is rendered with strftime, so
		// anything but a zero offset would be added to the
		// duration.
		setenv("TZ", "GMT", 1);
		tzset();
	}

	return option;
}

static void test_precision_variations(void)
{
	composite_time comp_time;
	time_t timestamp;

	// Test rounding up with precision level 3; hours increase due
	// to minutes and seconds.
	COMP_TIME_INIT(comp_time, 1, 2, 3, 45, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 1, 2, 4, 0, 0);

	// Test detailed representation with precision level 4;
	// minutes increase due to seconds.
	COMP_TIME_INIT(comp_time, 1, 2, 3, 45, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 1, 2, 3, 46, 0);

	// Test with 59 minutes, 59 seconds at precision level 4;
	// expecting no change since all units are significant.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 59, 59);

	// Test with 1 hour, 59 minutes, 59 seconds at precision level
	// 3; expecting no change as rounding not applied due to
	// precision.
	COMP_TIME_INIT(comp_time, 0, 0, 1, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 1, 59, 59);

	// Test rounding up from minutes to hours with precision level
	// 2; hours should increase, minutes reset.
	COMP_TIME_INIT(comp_time, 0, 0, 1, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 2, 0, 0);

	// Simplify to most significant unit (days) with precision
	// level 1 from days, hours, and minutes.
	COMP_TIME_INIT(comp_time, 0, 1, 2, 28, 30);
	timestamp = composite_time_to_seconds(comp_time);
	assert(timestamp == 95310);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(1, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 0, 0, 0);

	// Retain days and hours with precision level 2, simplifying
	// minutes and seconds.
	COMP_TIME_INIT(comp_time, 0, 1, 2, 28, 30);
	timestamp = composite_time_to_seconds(comp_time);
	assert(timestamp == 95310);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 2, 0, 0);

	// Retain days, hours, and approximate minutes with precision
	// level 3.
	COMP_TIME_INIT(comp_time, 0, 1, 2, 28, 30);
	timestamp = composite_time_to_seconds(comp_time);
	assert(timestamp == 95310);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 2, 29, 0);

	// Full detail maintained with precision level 4, no
	// approximation.
	COMP_TIME_INIT(comp_time, 0, 1, 2, 28, 30);
	timestamp = composite_time_to_seconds(comp_time);
	assert(timestamp == 95310);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 2, 28, 30);

	// Test high precision with minimal input (1 second), expecting no approximation.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 0, 1);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 0, 1);

	// Test for minute to hour rollover with precision level 2.
	COMP_TIME_INIT(comp_time, 0, 0, 1, 59, 30);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 2, 0, 0);

	// Test for no change with hours near maximum but within
	// precision level 2.
	COMP_TIME_INIT(comp_time, 0, 0, 23, 45, 0);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 23, 45, 0);

	// Test day to year rollover with precision level 2; days
	// reset, year increments.
	COMP_TIME_INIT(comp_time, 1, 364, 23, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 2, 0, 0, 0, 0);

	// Test handling when non-zero units exceed precision level 2;
	// hours and less significant units reset.
	COMP_TIME_INIT(comp_time, 0, 0, 23, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 0, 0, 0);

	// Test handling when all units are set to zero, with precision level 2.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 0, 0);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 0, 0);

	// Test edge case for seconds rolling over to minutes, with
	// precision level 2.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 59, 59);

	// Test high precision with minimal input (1 second), expecting no approximation.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 0, 1);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 0, 1);

	// Test rollover across multiple units (seconds to minutes to
	// hours), with precision level 3.
	COMP_TIME_INIT(comp_time, 0, 0, 23, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 23, 59, 59);

	// Test mixed zero and non-zero units, with precision level 2.
	COMP_TIME_INIT(comp_time, 1, 0, 1, 0, 1);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 1, 0, 1, 0, 0);

	// Test days to years rollover with minimal hours input, with
	// precision level 2.
	COMP_TIME_INIT(comp_time, 0, 364, 23, 0, 0);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 364, 23, 0, 0);

	// Test with maximum values just before rollover for each
	// unit, with precision level 4. Expecting no change with
	// precision 4, as all units are significant.
	COMP_TIME_INIT(comp_time, 0, 364, 23, 59, 59);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);

	COMP_TIME_ASSERT(comp_time, 0, 364, 23, 59, 59);

	// Test handling of cascading rollover from minutes to hours
	// to days, with precision level 2.
	COMP_TIME_INIT(comp_time, 0, 0, 23, 59, 30);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(2, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 0, 0, 0);

	// Test transition at midnight with precision level 3.
	COMP_TIME_INIT(comp_time, 0, 0, 23, 59, 60);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 1, 0, 0, 0);

	// Test with minimal non-zero units and lower precision.
	COMP_TIME_INIT(comp_time, 0, 0, 0, 1, 30);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(1, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 0, 2, 0);

	// Test sparse non-zero units with high precision.
	COMP_TIME_INIT(comp_time, 1, 0, 0, 0, 5);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(4, comp_time);
	COMP_TIME_ASSERT(comp_time, 1, 0, 0, 0, 5);

	// Test rounding in the middle of the spectrum with precision
	// 3. Assuming no change as it's already concise.
	COMP_TIME_INIT(comp_time, 0, 0, 12, 30, 0);
	timestamp = composite_time_to_seconds(comp_time);
	seconds_to_composite_time(timestamp, comp_time);
	approximate_time(3, comp_time);
	COMP_TIME_ASSERT(comp_time, 0, 0, 12, 30, 0);
}

static void test_mode_resolution(void)
{
	struct ts_opt opt;

	// -r supersedes the incremental flags.
	opt = (struct ts_opt){ .flag_rel = true, .flag_inc = true };
	resolve_modes(&opt);
	assert(opt.flag_rel && !opt.flag_inc && !opt.flag_sincestart);

	opt = (struct ts_opt){ .flag_rel = true, .flag_sincestart = true };
	resolve_modes(&opt);
	assert(opt.flag_rel && !opt.flag_inc && !opt.flag_sincestart);

	// Both at once are discarded too, so they never reach the
	// check that rejects them as a contradictory pair.
	opt = (struct ts_opt){ .flag_rel = true, .flag_inc = true, .flag_sincestart = true };
	resolve_modes(&opt);
	assert(opt.flag_rel && !opt.flag_inc && !opt.flag_sincestart);

	// Without -r the incremental flags survive untouched.
	opt = (struct ts_opt){ .flag_inc = true };
	resolve_modes(&opt);
	assert(!opt.flag_rel && opt.flag_inc && !opt.flag_sincestart);

	opt = (struct ts_opt){ .flag_sincestart = true };
	resolve_modes(&opt);
	assert(!opt.flag_rel && !opt.flag_inc && opt.flag_sincestart);

	// -m selects the clock rather than the mode, so it survives.
	opt = (struct ts_opt){ .flag_rel = true, .flag_inc = true, .flag_mono = true };
	resolve_modes(&opt);
	assert(opt.flag_mono);
}

static void test_format_resolution(void)
{
	const struct ts_opt absolute = { 0 };
	const struct ts_opt relative = { .flag_rel = true };
	const struct ts_opt incremental = { .flag_inc = true };
	const struct ts_opt sincestart = { .flag_sincestart = true };

	// Absolute and relative timestamps share the default format.
	// A relative run has had its incremental flags cleared by
	// resolve_modes, so -r -s formats as plain -r does.
	assert(strcmp(resolve_format(&absolute, NULL), "%b %d %H:%M:%S") == 0);
	assert(strcmp(resolve_format(&relative, NULL), "%b %d %H:%M:%S") == 0);

	// Elapsed times default to a bare clock.
	assert(strcmp(resolve_format(&incremental, NULL), "%H:%M:%S") == 0);
	assert(strcmp(resolve_format(&sincestart, NULL), "%H:%M:%S") == 0);

	// A format on the command line wins in every mode.
	assert(strcmp(resolve_format(&absolute, "%F"), "%F") == 0);
	assert(strcmp(resolve_format(&relative, "%F"), "%F") == 0);
	assert(strcmp(resolve_format(&incremental, "%F"), "%F") == 0);
	assert(strcmp(resolve_format(&sincestart, "%F"), "%F") == 0);
}

static void test_utc_conversion(void)
{
	struct tm tm;

	// Epoch, and dates either side of it.
	assert(days_from_civil(1970, 1, 1) == 0);
	assert(days_from_civil(1970, 1, 2) == 1);
	assert(days_from_civil(1969, 12, 31) == -1);

	// Century and leap rules: 2000 was a leap year, 1900 was not.
	assert(days_from_civil(2000, 3, 1) - days_from_civil(2000, 2, 28) == 2);
	assert(days_from_civil(1900, 3, 1) - days_from_civil(1900, 2, 28) == 1);

	memset(&tm, 0, sizeof tm);
	tm.tm_year = 1970 - 1900; tm.tm_mon = 0; tm.tm_mday = 1;
	assert(tm_to_utc_seconds(&tm) == 0);

	// 1994-06-16T07:29:35Z.
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 1994 - 1900; tm.tm_mon = 5; tm.tm_mday = 16;
	tm.tm_hour = 7; tm.tm_min = 29; tm.tm_sec = 35;
	assert(tm_to_utc_seconds(&tm) == 771751775);

	// The 32-bit time_t rollover, 2038-01-19T03:14:07Z.
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 2038 - 1900; tm.tm_mon = 0; tm.tm_mday = 19;
	tm.tm_hour = 3; tm.tm_min = 14; tm.tm_sec = 7;
	assert(tm_to_utc_seconds(&tm) == 2147483647);

	// A date before the epoch must go negative rather than wrap.
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 1969 - 1900; tm.tm_mon = 11; tm.tm_mday = 31;
	tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59;
	assert(tm_to_utc_seconds(&tm) == -1);

	// Offsets are read from the tail of the matched text.
	const char *zulu = "16 Jun 94 07:29:35 +0000";
	assert(parse_utc_offset(zulu, strlen(zulu)) == 0);

	const char *india = "16 Jun 94 07:29:35 +0530";
	assert(parse_utc_offset(india, strlen(india)) == 5 * 3600 + 30 * 60);

	const char *pacific = "16 Jun 94 07:29:35 -0800";
	assert(parse_utc_offset(pacific, strlen(pacific)) == -8 * 3600);

	// Text without an offset contributes nothing.
	const char *bare = "16 Jun 94 07:29:35";
	assert(parse_utc_offset(bare, strlen(bare)) == 0);
}

static volatile sig_atomic_t signal_received;

static void signal_handler(int sig)
{
	signal_received = sig;
}

int main(int argc, char *argv[])
{
	test_precision_variations();
	test_mode_resolution();
	test_format_resolution();
	test_utc_conversion();

	struct sigaction sa_sigint;
	sa_sigint.sa_handler = signal_handler;
	sa_sigint.sa_flags = 0;
	sigemptyset(&sa_sigint.sa_mask);

	if (sigaction(SIGINT, &sa_sigint, NULL) == -1) {
		perror("sigaction(SIGINT)");
		exit(EXIT_FAILURE);
	}

	struct sigaction sa_sigterm;
	sa_sigterm.sa_handler = signal_handler;
	sa_sigterm.sa_flags = 0;
	sigemptyset(&sa_sigterm.sa_mask);

	if (sigaction(SIGTERM, &sa_sigterm, NULL) == -1) {
		perror("sigaction(SIGTERM)");
		exit(EXIT_FAILURE);
	}

	if (setvbuf(stdout, NULL, _IOLBF, BUFSIZ) != 0) {
		perror("setvbuf");
		exit(EXIT_FAILURE);
	}

	must_init_timestamp_patterns();

	struct ts_opt opt = parse_options(argc, argv);
	struct ts_fmt fmt = { .opt = &opt };

	long secs = 0;
	long nsecs = 0;
	long monodelta = 0;

	if (!init_clocks(&opt, &secs, &nsecs, &monodelta)) {
		perror("init clocks");
		exit(EXIT_FAILURE);
	}

	int rc = sanitise_time_format(opt.format,
				      &fmt.sanitised_time_format,
				      &fmt.n_microseconds_specifiers,
				      opt.flag_rel ? COLLAPSE_MICROSECOND_SPECFIERS : EXPAND_MICROSECOND_SPECIFIERS);
	if (rc != 0) {
		perror("parse time format");
		exit(EXIT_FAILURE);
	}

	rc = validate_time_format(fmt.sanitised_time_format,
				  &fmt.buf,
				  &fmt.bufsz);
	if (rc != 0) {
		perror("strftime");
		exit(EXIT_FAILURE);
	}

	char *line = NULL;
	ssize_t line_len;
	size_t line_capacity = 0;

	while (!signal_received) {
		line_len = getline(&line, &line_capacity, stdin);

		if (line_len == -1) {
			if ((errno == EINTR && signal_received) || feof(stdin)) {
				break;
			} else if (errno != EINTR) {
				perror("getline");
				break;
			}
		}

		struct timespec now;
		if (!gettime(&opt, &now, &secs, &nsecs, monodelta)) {
			perror("gettime");
			break;
		}

		size_t offset = 0;

		if (opt.flag_rel)
			fmt_time_rel(&fmt, line, line_len, &offset, now);
		else
			fmt_time_now(&fmt, now);

		if (printf("%s%s%s", fmt.buf, opt.flag_rel ? "" : " ", line + offset) < 0) {
			perror("write");
			break;
		}
	}

	free(line);
	free(fmt.sanitised_time_format);
	free(fmt.buf);

	for (size_t i = 0; i < NELEMENTS(timestamps); i++) {
		pcre2_code_free(timestamps[i].pcre);
		pcre2_match_data_free(timestamps[i].match_data);
	}

	if (fflush(stdout) != 0) {
		perror("fflush");
		exit(EXIT_FAILURE);
	}

	return EXIT_SUCCESS;
}
