/*
 * adcli
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#include "adcli.h"
#include "adprivate.h"
#include "seq.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static adcli_message_func message_func = NULL;
static char last_error[2048] = { 0, };

void
_adcli_precond_failed (const char *message,
                       ...)
{
	va_list va;
	const char *env;

	va_start (va, message);
	vfprintf (stderr, message, va);
	va_end (va);

	env = getenv ("ADCLI_STRICT");
	if (env != NULL && env[0] != '\0')
		abort ();
}

const char *
adcli_result_to_string (adcli_result res)
{
	switch (res) {
	case ADCLI_SUCCESS:
		return "Success";
	case ADCLI_ERR_UNEXPECTED:
		return "Unexpected or internal system error";
	case ADCLI_ERR_DIRECTORY:
		return "Problem with the Active Directory or connecting to it";
	case ADCLI_ERR_CREDENTIALS:
		return "The administrative credentials are invalid or access is denied";
	case ADCLI_ERR_CONFIG:
		return "The local system has an invalid configuration";
	case ADCLI_ERR_FAIL:
		return "Generic failure";
	}

	return_val_if_reached ("Unknown error");
}

static void
messagev (adcli_message_type type,
          const char *format,
          va_list va)
{
	char buffer[sizeof (last_error)];
	char *where = buffer;
	int ret;

	if (type == ADCLI_MESSAGE_ERROR)
		where = last_error;
	else if (message_func == NULL)
		return;

	ret = vsnprintf (where, sizeof (buffer), format, va);
	return_if_fail (ret >= 0);

	if (message_func != NULL)
		(message_func) (type, where);
}

void
_adcli_err (const char *format,
            ...)
{
	va_list va;
	va_start (va, format);
	messagev (ADCLI_MESSAGE_ERROR, format, va);
	va_end (va);
}

void
_adcli_warn (const char *format,
             ...)
{
	va_list va;
	va_start (va, format);
	messagev (ADCLI_MESSAGE_ERROR, format, va);
	va_end (va);
}

void
_adcli_info (const char *format,
             ...)
{
	va_list va;
	va_start (va, format);
	messagev (ADCLI_MESSAGE_INFO, format, va);
	va_end (va);
}

void
adcli_set_message_func (adcli_message_func func)
{
	message_func = func;
}

const char *
adcli_get_last_error (void)
{
	return last_error[0] ? last_error : NULL;
}

void
adcli_clear_last_error (void)
{
	last_error[0] = '\0';
}

void
_adcli_strv_free (char **strv)
{
	seq_free (strv, free);
}

char **
_adcli_strv_dup (char **strv)
{
	int count;

	if (!strv)
		return NULL;

	count = seq_count (strv);
	return seq_dup (strv, &count, (seq_copy)strdup);
}

char *
_adcli_strv_join (char **strv,
                  const char *delim)
{
	char *result = NULL;
	int at = 0;
	int dlen;
	int slen;
	int i;

	dlen = strlen (delim);
	for (i = 0; strv && strv[i] != NULL; i++) {
		slen = strlen (strv[i]);
		result = realloc (result, at + dlen + slen + 1);
		return_val_if_fail (result != NULL, NULL);
		if (at != 0) {
			memcpy (result + at, delim, dlen);
			at += dlen;
		}

		memcpy (result + at, strv[i], slen);
		at += slen;
		result[at] = '\0';
	}

	return result;
}

int
_adcli_strv_len (char **strv)
{
	return seq_count (strv);
}

char **
_adcli_strv_add (char **strv,
                 char *string,
                 int *length)
{
	int len;

	return_val_if_fail (string != NULL, strv);

	if (!length) {
		len = seq_count (strv);
		length = &len;
	}

	return seq_push (strv, length, string);
}

void
_adcli_str_up (char *str)
{
	while (*str != '\0') {
		*str = toupper (*str);
		str++;
	}
}

void
_adcli_str_down (char *str)
{
	while (*str != '\0') {
		*str = tolower (*str);
		str++;
	}
}

void
_adcli_str_set (char **field,
                const char *value)
{
	char *newval = NULL;

	if (value) {
		newval = strdup (value);
		return_if_fail (newval != NULL);
	}

	free (*field);
	*field = newval;
}

void
_adcli_strv_set (char ***field,
                 const char **value)
{
	char **newval = NULL;

	if (value) {
		newval = _adcli_strv_dup ((char **)value);
		return_if_fail (newval != NULL);
	}

	_adcli_strv_free (*field);
	*field = newval;
}

char *
_adcli_str_dupn (void *data,
                 size_t len)
{
	char *result;

	result = malloc (len + 1);
	return_val_if_fail (result, NULL);

	memcpy (result, data, len);
	result[len] = '\0';
	return result;
}

int
_adcli_password_free (char *password)
{
	int ret;

	if (password == NULL)
		return 0;

	ret = adcli_mem_clear (password, strlen (password));
	free (password);
	return ret;
}

int
adcli_mem_clear (void *data,
                 size_t length)
{
	volatile char *vp;
	int ret = 0;

	if (data == NULL)
		return 0;

	/*
	 * Cracktastic stuff here to help compilers not
	 * optimize this away
	 */

	vp = (volatile char*)data;
	while (length) {
		*vp = 0xAA;
		ret += *vp;
		vp++;
		length--;
	}

	return ret;
}

int
_adcli_write_all (int fd,
                  const char *buf,
                  int len)
{
	int res;

	if (len == -1)
		len = strlen (buf);

	while (len > 0) {
		res = write (fd, buf, len);
		if (res <= 0) {
			if (errno == EAGAIN && errno == EINTR)
				continue;
			return -errno;
		} else  {
			len -= res;
			buf += res;
		}
	}

	return 0;
}

#ifdef UTIL_TESTS

#include "test.h"

static void
test_strv_add_free (void)
{
	char **strv = NULL;

	strv = _adcli_strv_add (strv, strdup ("one"), NULL);
	strv = _adcli_strv_add (strv, strdup ("two"), NULL);
	strv = _adcli_strv_add (strv, strdup ("three"), NULL);

	assert_str_eq (strv[0], "one");
	assert_str_eq (strv[1], "two");
	assert_str_eq (strv[2], "three");
	assert (strv[3] == NULL);

	_adcli_strv_free (strv);
}

static void
test_strv_dup (void)
{
	char *values[] = { "one", "two", "three", NULL };
	char **strv;

	strv = _adcli_strv_dup (values);

	assert_str_eq (strv[0], "one");
	assert_str_eq (strv[1], "two");
	assert_str_eq (strv[2], "three");
	assert (strv[3] == NULL);

	_adcli_strv_free (strv);
}

static void
test_strv_count (void)
{
	char *values[] = { "one", "two", "three", NULL };
	int len;

	len = _adcli_strv_len (values);
	assert_num_eq (len, 3);
}

int
main (int argc,
      char *argv[])
{
	test_func (test_strv_add_free, "/util/strv_add_free");
	test_func (test_strv_dup, "/util/strv_dup");
	test_func (test_strv_count, "/util/strv_count");
	return test_run (argc, argv);
}

#endif /* UTIL_TESTS */
