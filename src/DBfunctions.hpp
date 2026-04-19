#pragma once
#include <libpq-fe.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <chrono>

std::vector<std::vector<std::string>> returnResult(PGconn* conn, const char* query); 

std::vector<std::vector<std::string>> extract_Data_From_CSV(const std::string& filepath);

void debug_print(std::vector<std::vector<std::string>>& table);

bool insert_segments_as_nodes(PGconn* conn, const std::vector<std::vector<std::string>>& data); 

bool insert_points_as_nodes_normal_graph(PGconn* conn, const std::vector<std::vector<std::string>>& data); 

bool add_edges(PGconn* conn, std::vector<std::vector<std::string>>& data);

bool add_segments_as_edges_to_normal_graph(PGconn* conn, std::vector<std::vector<std::string>>& data);
