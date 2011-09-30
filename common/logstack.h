/*
 * logstack.h
 *
 *  Created on: 27 Sep 2011
 *      Author: Jim
 */

#ifndef LOGSTACK_H_
#define LOGSTACK_H_

#include <execinfo.h>

inline void logstack()
{
	void * array[50];
	int size = backtrace(array, 50);

	LogError("EXC: backtrace returned " << size << " frames");

	char ** messages = backtrace_symbols(array, size);

	// skip first stack frame (points here)
	for (int i = 1; i < size && messages != NULL; ++i)
		LogError("EXC: (" << i << ") " << messages[i]);

	free(messages);
}

#endif /* LOGSTACK_H_ */
