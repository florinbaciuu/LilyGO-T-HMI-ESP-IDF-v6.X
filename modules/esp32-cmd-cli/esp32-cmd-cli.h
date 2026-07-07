#pragma once
#ifndef COMMAND_LINE_INTERFACE_H_
#define COMMAND_LINE_INTERFACE_H_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#warning "Acest cod este scris de Florin Aurel Baciu"
#pragma GCC diagnostic pop

#include "config.h"



//=======================================================



//=======================================================

extern char prompt[CONSOLE_PROMPT_MAX_LEN];

//=======================================================

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

//--------------------------------------------------------

void start_cli_task();

//--------------------------------------------------------

#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */
#endif /* #ifndef COMMAND_LINE_INTERFACE_H_ */
