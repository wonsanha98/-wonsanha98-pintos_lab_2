#include <string.h>
#include <debug.h>

/* Copies SIZE bytes from SRC to DST, which must not overlap.
   Returns DST. */
void *
memcpy (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	while (size-- > 0)
		*dst++ = *src++;

	return dst_;
}

/* Copies SIZE bytes from SRC to DST, which are allowed to
   overlap.  Returns DST. */
void *
memmove (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	if (dst < src) {
		while (size-- > 0)
			*dst++ = *src++;
	} else {
		dst += size;
		src += size;
		while (size-- > 0)
			*--dst = *--src;
	}

	return dst;
}

/* SIZE 바이트만큼의 두 메모리 블록 A와 B를 비교하여,
처음으로 다른 바이트를 찾습니다.
A 쪽 바이트가 더 크면 양수를,
B 쪽 바이트가 더 크면 음수를,
두 블록이 같으면 0을 반환합니다. */
int
memcmp (const void *a_, const void *b_, size_t size) {
	const unsigned char *a = a_;
	const unsigned char *b = b_;

	ASSERT (a != NULL || size == 0);
	ASSERT (b != NULL || size == 0);

	for (; size-- > 0; a++, b++)
		if (*a != *b)
			return *a > *b ? +1 : -1;
	return 0;
}

/* 문자열 A와 B에서 처음으로 다른 문자를 찾습니다.
A의 문자가 (부호 없는 문자로서) 더 크면 양수를,
B의 문자가 더 크면 음수를 반환하며,
A와 B가 동일한 문자열이면 0을 반환합니다. */
int
strcmp (const char *a_, const char *b_) {
	const unsigned char *a = (const unsigned char *) a_;
	const unsigned char *b = (const unsigned char *) b_;

	ASSERT (a != NULL);
	ASSERT (b != NULL);

	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}

	return *a < *b ? -1 : *a > *b;
}

/* BLOCK에서 시작하는 처음 SIZE 바이트 내에서
문자 CH가 처음 나타나는 위치의 포인터를 반환합니다.
만약 CH가 BLOCK 내에 존재하지 않으면 널 포인터(null pointer) 를 반환합니다. */
void *
memchr (const void *block_, int ch_, size_t size) {
	const unsigned char *block = block_;
	unsigned char ch = ch_;

	ASSERT (block != NULL || size == 0);

	for (; size-- > 0; block++)
		if (*block == ch)
			return (void *) block;

	return NULL;
}

/* STRING에서 문자 C가 처음 나타나는 위치를 찾아 그 포인터를 반환합니다.
만약 C가 STRING에 없다면 널 포인터(null pointer) 를 반환합니다.
단, C == '\0'인 경우에는 문자열 끝의 널 종료 문자(null terminator) 에 대한 포인터를 반환합니다. */
char *
strchr (const char *string, int c_) {
	char c = c_;

	ASSERT (string);

	for (;;)
		if (*string == c)
			return (char *) string;
		else if (*string == '\0')
			return NULL;
		else
			string++;
}

/* STRING의 접두 부분 중에서
STOP에 포함되지 않은 문자들로만 이루어진
최초 부분 문자열의 길이를 반환합니다. */
size_t
strcspn (const char *string, const char *stop) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (stop, string[length]) != NULL)
			break;
	return length;
}

/* STRING에서 STOP에 포함된 문자가 처음으로 나타나는 위치의 포인터를 반환합니다.
만약 STRING에 STOP에 포함된 문자가 하나도 없다면, 널 포인터(null pointer) 를 반환합니다. */
char *
strpbrk (const char *string, const char *stop) {
	for (; *string != '\0'; string++)
		if (strchr (stop, *string) != NULL)
			return (char *) string;
	return NULL;
}

/* STRING에서 문자 C가 마지막으로 나타나는 위치의 포인터를 반환합니다.
만약 C가 STRING에 존재하지 않으면 널 포인터(null pointer) 를 반환합니다. */
char *
strrchr (const char *string, int c_) {
	char c = c_;
	const char *p = NULL;

	for (; *string != '\0'; string++)
		if (*string == c)
			p = string;
	return (char *) p;
}

/* STRING의 접두 부분 중에서
SKIP에 포함된 문자들로만 이루어진 최초 부분 문자열의 길이를 반환합니다. */
size_t
strspn (const char *string, const char *skip) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (skip, string[length]) == NULL)
			break;
	return length;
}

/* HAYSTACK 내에서 NEEDLE이 처음으로 나타나는 위치를 가리키는 포인터를 반환합니다.
NEEDLE이 HAYSTACK 내에 존재하지 않으면 널 포인터를 반환합니다. */
char *
strstr (const char *haystack, const char *needle) {
	size_t haystack_len = strlen (haystack);
	size_t needle_len = strlen (needle);

	if (haystack_len >= needle_len) {
		size_t i;

		for (i = 0; i <= haystack_len - needle_len; i++)
			if (!memcmp (haystack + i, needle, needle_len))
				return (char *) haystack + i;
	}

	return NULL;
}

/* 문자열을 DELIMITERS(구분자)로 구분된 토큰들로 나눕니다.
이 함수를 처음 호출할 때는 S에 토크나이즈할 문자열을 넘겨야 하며, 이후의 호출에서는 S에 널 포인터를 전달해야 합니다.
SAVE_PTR는 토크나이저의 현재 위치를 추적하기 위해 사용되는 char * 변수의 주소입니다.
함수는 호출될 때마다 문자열 내의 다음 토큰을 반환하며, 더 이상 토큰이 없으면 널 포인터를 반환합니다.

이 함수는 인접한 여러 개의 구분자를 하나의 구분자로 처리합니다.
반환되는 토큰은 길이가 0인 경우가 없습니다.
하나의 문자열 안에서도 호출마다 DELIMITERS를 변경할 수 있습니다.


strtok_r() 함수는 문자열 S를 수정하여 구분자를 널 바이트(null byte)로 변경합니다.
따라서 S는 수정 가능한 문자열이어야 합니다.
특히 문자열 리터럴은 C 언어에서 이전 버전과의 호환성을 위해 const로 선언되지 않더라도 수정할 수 없습니다.

   Example usage:

   char s[] = "  String to  tokenize. ";
   char *token, *save_ptr;

   for (token = strtok_r (s, " ", &save_ptr); token != NULL;
   token = strtok_r (NULL, " ", &save_ptr))
   printf ("'%s'\n", token);

outputs:

'String'
'to'
'tokenize.'
*/
char *
strtok_r (char *s, const char *delimiters, char **save_ptr) {
	char *token;

	ASSERT (delimiters != NULL);
	ASSERT (save_ptr != NULL);

	/* If S is nonnull, start from it.
	   If S is null, start from saved position. */
	if (s == NULL)
		s = *save_ptr;
	ASSERT (s != NULL);

	/* Skip any DELIMITERS at our current position. */
	while (strchr (delimiters, *s) != NULL) {
		/* strchr() will always return nonnull if we're searching
		   for a null byte, because every string contains a null
		   byte (at the end). */
		if (*s == '\0') {
			*save_ptr = s;
			return NULL;
		}

		s++;
	}

	/* Skip any non-DELIMITERS up to the end of the string. */
	token = s;
	while (strchr (delimiters, *s) == NULL)
		s++;
	if (*s != '\0') {
		*s = '\0';
		*save_ptr = s + 1;
	} else
		*save_ptr = s;
	return token;
}

/* Sets the SIZE bytes in DST to VALUE. */
void *
memset (void *dst_, int value, size_t size) {
	unsigned char *dst = dst_;

	ASSERT (dst != NULL || size == 0);

	while (size-- > 0)
		*dst++ = value;

	return dst_;
}

/* Returns the length of STRING. */
size_t
strlen (const char *string) {
	const char *p;

	ASSERT (string);

	for (p = string; *p != '\0'; p++)
		continue;
	return p - string;
}

/* If STRING is less than MAXLEN characters in length, returns
   its actual length.  Otherwise, returns MAXLEN. */
size_t
strnlen (const char *string, size_t maxlen) {
	size_t length;

	for (length = 0; string[length] != '\0' && length < maxlen; length++)
		continue;
	return length;
}

/* Copies string SRC to DST.  If SRC is longer than SIZE - 1
   characters, only SIZE - 1 characters are copied.  A null
   terminator is always written to DST, unless SIZE is 0.
   Returns the length of SRC, not including the null terminator.

   strlcpy() is not in the standard C library, but it is an
   increasingly popular extension.  See
http://www.courtesan.com/todd/papers/strlcpy.html for
information on strlcpy(). */
size_t
strlcpy (char *dst, const char *src, size_t size) {
	size_t src_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	if (size > 0) {
		size_t dst_len = size - 1;
		if (src_len < dst_len)
			dst_len = src_len;
		memcpy (dst, src, dst_len);
		dst[dst_len] = '\0';
	}
	return src_len;
}

/* Concatenates string SRC to DST.  The concatenated string is
   limited to SIZE - 1 characters.  A null terminator is always
   written to DST, unless SIZE is 0.  Returns the length that the
   concatenated string would have assuming that there was
   sufficient space, not including a null terminator.

   strlcat() is not in the standard C library, but it is an
   increasingly popular extension.  See
http://www.courtesan.com/todd/papers/strlcpy.html for
information on strlcpy(). */
size_t
strlcat (char *dst, const char *src, size_t size) {
	size_t src_len, dst_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	dst_len = strlen (dst);
	if (size > 0 && dst_len < size) {
		size_t copy_cnt = size - dst_len - 1;
		if (src_len < copy_cnt)
			copy_cnt = src_len;
		memcpy (dst + dst_len, src, copy_cnt);
		dst[dst_len + copy_cnt] = '\0';
	}
	return src_len + dst_len;
}

