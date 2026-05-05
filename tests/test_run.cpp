#include<cassert>
#include "DBfunctions.hpp"
#include <libpq-fe.h>

#define assertm(exp, msg) assert((void(msg), exp))

int main(){

    assertm(2 == 3, "bab");
    return 0; 
}