#ifndef FILESYS_OFF_T_H
#define FILESYS_OFF_T_H

#include <stdint.h>

/* An offset within a file, 파일 내부의 위치?.
 * 여러 파일이 이 정의만을 필요로 하고, 다른 정의들은 원하지 않기 때문에
 * 별도의 헤더로 분리된 것이다.. */
typedef int32_t off_t;

/* Format specifier for printf(), e.g.:
 * printf ("offset=%"PROTd"\n", offset); */
#define PROTd PRId32

#endif /* filesys/off_t.h */
