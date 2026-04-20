#pragma once
#include <libpq-fe.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <chrono>


//TODO(nkc): turn into struct that has a map of each column linked to each column
// Operator overloads
// behavior res->"HELLO" fetches info from column "HELLO"
// behavior res->1 performs .at(1) on internal table
typedef std::vector<std::vector<std::string>> db_result_t;

db_result_t returnResult(PGconn* conn, const char* query); 

db_result_t extract_Data_From_CSV(const std::string& filepath);

void debug_print(db_result_t& table);

bool insert_segments_as_nodes(PGconn* conn, const db_result_t& data); 

bool add_edges(PGconn* conn, db_result_t& data);