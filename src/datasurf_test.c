#include "datasurf_main.h"
#include "datasurf_print_macros.h"

#include <stdio.h>

int main
(
    void
){
    fprintf(stderr, "Hello world from this test!\n");
    DS_DEBUG("I am a debug print!");
    return 0;
}
