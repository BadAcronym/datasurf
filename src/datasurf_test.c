#include "datasurf_main.h"
#include "pd_print_macros.h"

#include <stdio.h>

int main
(
    void
){
    fprintf(stderr, "Hello world from this test!\n");
    PD_DEBUG("I am a debug print!");
    return 0;
}
