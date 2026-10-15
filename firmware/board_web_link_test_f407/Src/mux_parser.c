#include "mux_parser.h"
#include <stddef.h>
#include <string.h>

static char s_mux_line[256];
static uint16_t s_mux_used;
static uint8_t s_mux_drop;
static uint8_t s_mux_link;
static uint32_t s_mux_remaining;
static uint32_t s_mux_errors;
static MuxLineCallback s_mux_on_line;
static MuxDataCallback s_mux_on_data;
static MuxPromptCallback s_mux_on_prompt;

void MuxParser_Reset(void)
{
    s_mux_used = 0U;
    s_mux_drop = 0U;
    s_mux_remaining = 0U;
}

void MuxParser_Init(MuxLineCallback line, MuxDataCallback data, MuxPromptCallback prompt)
{
    s_mux_on_line = line;
    s_mux_on_data = data;
    s_mux_on_prompt = prompt;
    s_mux_errors = 0U;
    MuxParser_Reset();
}

static int mux_header(void)
{
    uint32_t length = 0U;
    uint16_t i;
    /* CIPMUX=1, CIPDINFO=0: +IPD,<0..4>,<length>: */
    if (s_mux_used < 8U || s_mux_line[5] < '0' || s_mux_line[5] > '4' ||
        s_mux_line[6] != ',') return 0;
    for (i = 7U; i < s_mux_used; ++i)
    {
        const char c = s_mux_line[i];
        if (c < '0' || c > '9' || length > 104857U) return 0;
        length = length * 10U + (uint32_t)(c - '0');
    }
    if (length > 1048576U) return 0;
    s_mux_link = (uint8_t)(s_mux_line[5] - '0');
    s_mux_remaining = length;
    return 1;
}

void MuxParser_Feed(uint8_t byte)
{
    if (s_mux_remaining != 0U)
    {
        --s_mux_remaining;
        if (s_mux_on_data != NULL) s_mux_on_data(s_mux_link, byte);
        return;
    }
    if (byte == '\n')
    {
        if (!s_mux_drop && s_mux_used != 0U)
        {
            s_mux_line[s_mux_used] = '\0';
            if (s_mux_on_line != NULL) s_mux_on_line(s_mux_line);
        }
        s_mux_used = 0U;
        s_mux_drop = 0U;
        return;
    }
    if (s_mux_drop || byte == '\r') return;
    if (s_mux_used == 0U && byte == ' ') return;
    if (s_mux_used == 0U && byte == '>')
    {
        if (s_mux_on_prompt != NULL) s_mux_on_prompt();
        return;
    }
    if (byte == ':' && s_mux_used >= 5U && strncmp(s_mux_line, "+IPD,", 5U) == 0)
    {
        if (!mux_header()) { ++s_mux_errors; s_mux_drop = 1U; }
        s_mux_used = 0U;
        return;
    }
    if (s_mux_used >= sizeof(s_mux_line) - 1U)
    {
        ++s_mux_errors;
        s_mux_drop = 1U;
        s_mux_used = 0U;
        return;
    }
    s_mux_line[s_mux_used++] = (char)byte;
}

uint32_t MuxParser_Errors(void) { return s_mux_errors; }
