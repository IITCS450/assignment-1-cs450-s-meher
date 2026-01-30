#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *a){fprintf(stderr,"Usage: %s <pid>\n",a); exit(1);}
static int isnum(const char*s){ if(!s||!*s) return 0; for(;*s;s++) if(!isdigit((unsigned char)*s)) return 0; return 1; }

static int read_cmdline(pid_t pid, char *out, size_t outsz){
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if(!f) return -1;

    size_t n = fread(out, 1, outsz-1, f);
    fclose(f);
    out[n] = '\0';

    if(n == 0) { out[0] = '\0'; return 0; }

    // cmdline is NUL-separated, convert to spaces for printing
    for(size_t i=0;i<n;i++){
        if(out[i] == '\0') out[i] = ' ';
    }
    while(n>0 && out[n-1]==' ') out[--n]='\0';
    return 0;
}

static int read_vmrss(pid_t pid, long *vmrss_kb){
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if(!f) return -1;

    char line[512];
    while(fgets(line, sizeof(line), f)){
        if(strncmp(line, "VmRSS:", 6) == 0){
            char *p = line + 6;
            while(*p && isspace((unsigned char)*p)) p++;
            errno = 0;
            long val = strtol(p, NULL, 10);
            fclose(f);
            if(errno) return -2;
            *vmrss_kb = val;
            return 0;
        }
    }
    fclose(f);
    return -3;
}

static int read_stat(pid_t pid, char *state, int *ppid, long long *ut, long long *st, int *cpu_core){
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if(!f) return -1;

    char buf[4096];
    if(!fgets(buf, sizeof(buf), f)){ fclose(f); return -2; }
    fclose(f);

    char *lpar = strchr(buf, '(');
    char *rpar = strrchr(buf, ')');
    if(!lpar || !rpar || rpar < lpar) return -3;

    char *p = rpar + 2;
    if(!*p) return -4;

    *state = *p;
    p += 2;

    int field = 4;
    char *save = NULL;
    char *tok = strtok_r(p, " ", &save);

    int got_ppid=0, got_ut=0, got_st=0;
    *cpu_core = -1;

    while(tok){
        if(field == 4){ *ppid = atoi(tok); got_ppid=1; }
        else if(field == 14){ *ut = atoll(tok); got_ut=1; }
        else if(field == 15){ *st = atoll(tok); got_st=1; }
        else if(field == 39){ *cpu_core = atoi(tok); }
        field++;
        tok = strtok_r(NULL, " ", &save);
    }

    return (got_ppid && got_ut && got_st) ? 0 : -5;
}

int main(int c,char**v){
    if(c!=2||!isnum(v[1])) usage(v[0]);

    pid_t pid = (pid_t)atoi(v[1]);

    char state='?';
    int ppid=-1;
    int cpu_core = -1;              
    long long ut=0, st=0;
    long vmrss_kb=-1;
    char cmdline[4096]; cmdline[0]='\0';

    if(read_stat(pid, &state, &ppid, &ut, &st, &cpu_core) != 0){
        fprintf(stderr, "Error: cannot read process info\n");
        return 1;
    }
    if(read_cmdline(pid, cmdline, sizeof(cmdline)) != 0){
        fprintf(stderr, "Error: cannot read cmdline\n");
        return 1;
    }
    if(read_vmrss(pid, &vmrss_kb) != 0){
        fprintf(stderr, "Error: cannot read VmRSS\n");
        return 1;
    }

    long ticks = sysconf(_SC_CLK_TCK);
    double cpu_sec = 0.0;
    if(ticks > 0) cpu_sec = (double)(ut + st) / (double)ticks;

    printf("PID:%d\n", pid);
    printf("State:%c\n", state);
    printf("PPID:%d\n", ppid);
    printf("Cmd:%s\n", cmdline[0] ? cmdline : "(empty)");
    printf("CPU:%d %.3f\n", cpu_core, cpu_sec);
    printf("VmRSS:%ld\n", vmrss_kb);

    return 0;
}