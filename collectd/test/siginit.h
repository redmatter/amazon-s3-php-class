#ifndef _SIGINIT_H_
#define _SIGINIT_H_

/* save+quit */
void sigint(int s);

/* quit */
void sighup(int s);

/* kill */
void sigterm(int s);

/* save */
void sigusr1(int s);

/* reset all */
void sigusr2(int s);

/* sigpipe ?! */
void sigpipe(int s);

/* setup signals */
void siginit(void);

#endif // _SIGINIT_H_
