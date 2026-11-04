/* stand-in for unistd.h: only what priiloader's sources call */
#ifndef STUB_UNISTD_H
#define STUB_UNISTD_H
#ifdef __cplusplus
extern "C" {
#endif
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
#ifdef __cplusplus
}
#endif
#endif
