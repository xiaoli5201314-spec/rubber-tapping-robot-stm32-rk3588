#ifndef MUX_PARSER_H
#define MUX_PARSER_H
#include <stdint.h>
typedef void (*MuxLineCallback)(const char *);
typedef void (*MuxDataCallback)(uint8_t link, uint8_t byte);
typedef void (*MuxPromptCallback)(void);
void MuxParser_Init(MuxLineCallback line, MuxDataCallback data, MuxPromptCallback prompt);
void MuxParser_Reset(void);
void MuxParser_Feed(uint8_t byte);
uint32_t MuxParser_Errors(void);
#endif
