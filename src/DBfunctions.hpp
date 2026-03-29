#pragma once
#include <libpq-fe.h>
#include <iostream>
#include <vector>
std::vector<std::vector<std::string>> returnResult(PGconn* conn, const char* query); 
