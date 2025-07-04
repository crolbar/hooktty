#pragma once

#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_BLUE    "\x1b[34m"

static void
log_msg(const char* level,
        const char* file,
        int line,
        const char* func,
        const char* fmt,
        ...) 
{
    if ((strcmp(level, "DEBUG") == 0) && !getenv("HOOKTTY_DEBUG"))
        return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    if (strcmp(level, "INFO") == 0)
        fprintf(stdout, ANSI_CYAN);
    if (strcmp(level, "DEBUG") == 0)
        fprintf(stdout, ANSI_BLUE);
    if (strcmp(level, "WARNING") == 0)
        fprintf(stdout, ANSI_YELLOW);
    if (strcmp(level, "ERROR") == 0)
        fprintf(stdout, ANSI_RED);

    fprintf(stdout, "[%s] ", level);

    fprintf(stdout, "[%s] ", time_buf);

    if (strcmp(level, "DEBUG") == 0)
        fprintf(stdout, "%s:%d (%s()) ", file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fprintf(stdout, ANSI_RESET);
    fprintf(stdout, "\n");
}

#define HOG(fmt, ...) log_msg("DEBUG", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define HOG_ERR(fmt, ...) log_msg("ERROR", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define HOG_WARN(fmt, ...) log_msg("WARNING", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define HOG_INFO(fmt, ...) log_msg("INFO", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
