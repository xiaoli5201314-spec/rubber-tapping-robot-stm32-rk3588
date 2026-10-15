#ifndef AT_PARSER_H
#define AT_PARSER_H
#include <stdint.h>

typedef void (*AtLineCallback)(const char *line);
typedef void (*AtByteCallback)(uint8_t byte);
typedef void (*AtPromptCallback)(void);

/* Streaming parser separates AT lines, bare prompts and length-framed +IPD. */
void AtParser_Init(AtLineCallback line, AtByteCallback data,
                   AtPromptCallback prompt);
void AtParser_Reset(void);
void AtParser_Feed(uint8_t byte);
uint32_t AtParser_Errors(void);

#endif
