#ifndef __PS1CFW_ENABLER_H
#define __PS1CFW_ENABLER_H

int ps1cfw_enabler_start(tai_module_info_t tai_info);
int ps1cfw_enabler_stop();
int ps1cfw_open_filter(char file[256], int *custom_ret);
void ps1cfw_getstat_filter(char file[256]);

#endif

