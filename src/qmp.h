#pragma once

char *qmp_sockpath(void);
int qmp_listen(const char *sockpath);
int qmp_accept(int fd);
int qmp_command(int fd, const char *command);
