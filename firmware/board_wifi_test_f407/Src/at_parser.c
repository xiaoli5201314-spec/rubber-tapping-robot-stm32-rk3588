#include "at_parser.h"
#include <stddef.h>
#include <string.h>

static char s_line[768];
static uint16_t s_used;
static uint8_t s_drop;
static uint32_t s_ipd_remaining;
static uint32_t s_errors;
static AtLineCallback s_on_line;
static AtByteCallback s_on_data;
static AtPromptCallback s_on_prompt;

void AtParser_Reset(void)
{
    s_used = 0U;
    s_drop = 0U;
    s_ipd_remaining = 0U;
}

void AtParser_Init(AtLineCallback line, AtByteCallback data,
                   AtPromptCallback prompt)
{
    s_on_line = line;
    s_on_data = data;
    s_on_prompt = prompt;
    s_errors = 0U;
    AtParser_Reset();
}

static int ipd_length(uint32_t *out)
{
    uint32_t value = 0U;
    uint16_t i = 5U;
    uint8_t digits = 0U;

    /* CIPMUX=0, CIPDINFO=0: +IPD,<length>: only. */
    if (s_used <= i || strncmp(s_line, "+IPD,", 5U) != 0)
    {
        return 0;
    }
    while (i < s_used)
    {
        const char c = s_line[i++];
        if (c < '0' || c > '9' || value > 100000U)
        {
            return 0;
        }
        value = value * 10U + (uint32_t)(c - '0');
        digits = 1U;
    }
    if (!digits || value > 1048576U)
    {
        return 0;
    }
    *out = value;
    return 1;
}

void AtParser_Feed(uint8_t byte)
{
    if (s_ipd_remaining != 0U)
    {
        --s_ipd_remaining;
        if (s_on_data != NULL) s_on_data(byte);
        return;
    }
    if (byte == '\n')
    {
        if (!s_drop && s_used != 0U)
        {
            s_line[s_used] = '\0';
            if (s_on_line != NULL) s_on_line(s_line);
        }
        s_used = 0U;
        s_drop = 0U;
        return;
    }
    if (s_drop || byte == '\r') return;
    if (s_used == 0U && byte == ' ')
    {
        return;
    }
    if (s_used == 0U && byte == '>')
    {
        if (s_on_prompt != NULL) s_on_prompt();
        return;
    }
    if (byte == ':' && s_used >= 5U &&
        strncmp(s_line, "+IPD,", 5U) == 0)
    {
        if (!ipd_length(&s_ipd_remaining))
        {
            ++s_errors;
            s_drop = 1U;
        }
        s_used = 0U;
        return;
    }
    if (s_used >= sizeof(s_line) - 1U)
    {
        ++s_errors;
        s_drop = 1U;
        s_used = 0U;
        return;
    }
    s_line[s_used++] = (char)byte;
}

uint32_t AtParser_Errors(void)
{
    return s_errors;
}
