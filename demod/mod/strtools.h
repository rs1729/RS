#include <stdio.h> /* needed for vsnprintf */
#include <stdlib.h> /* needed for malloc-free */
#include <stdarg.h> /* needed for va_list */
#include <limits.h>

#ifndef _vscprintf
/* For some reason, MSVC fails to honour this #ifndef. */
/* Hence function renamed to _vscprintf_so(). */
int _vscprintf_so(const char * format, va_list pargs) {
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;}
#endif // _vscprintf

#ifndef vasprintf
int vasprintf(char **strp, const char *fmt, va_list ap) {
    int len = _vscprintf_so(fmt, ap);
    if (len == -1) return -1;
    char *str = malloc((size_t) len + 1);
    if (!str) return -1;
    int r = vsnprintf(str, len + 1, fmt, ap); /* "secure" version of vsprintf */
    if (r == -1) return free(str), -1;
    *strp = str;
    return r;}
#endif // vasprintf

#ifndef asprintf
int asprintf(char *strp[], const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;}
#endif // asprintf

#ifndef strsep
char *strsep(char **stringp, const char *delim) {
  if (*stringp == NULL) { return NULL; }
  char *token_start = *stringp;
  *stringp = strpbrk(token_start, delim);
  if (*stringp) {
    **stringp = '\0';
    (*stringp)++;
  }
  return token_start;
}
#endif

#ifndef strntol
long strntol(const char *str, size_t sz, char **end, int base)
{
	/* Expect that digit representation of LONG_MAX/MIN
	 * not greater than this buffer */
	char buf[24];
	long ret;
	const char *beg = str;

	/* Catch up leading spaces */
	for (; beg && sz && *beg == ' '; beg++, sz--)
		;

	if (!sz || sz >= sizeof(buf)) {
		if (end)
			*end = (char *)str;
		return 0;
	}

	memcpy(buf, beg, sz);
	buf[sz] = '\0';
	ret = strtol(buf, end, base);
	if (ret == LONG_MIN || ret == LONG_MAX)
		return ret;
	if (end)
		*end = (char *)beg + (*end - buf);
	return ret;
}
#endif
