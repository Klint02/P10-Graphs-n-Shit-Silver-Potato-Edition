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

class DBfunctions
{
private:
    PGconn* conn_;
    PGresult* res_;
    const std::string graph_name_;
    struct Segment {
        public:
        std::string segmentkey;
        std::string startpoint;
        std::string endpoint;
        std::string category;
        std::string direction;
        std::string name;
        std::string point;
        std::vector<std::string> municipality_keys;
    };
    
public:
    DBfunctions(const std::string& host,
                const std::string& port,
                const std::string& dbname,
                const std::string& username,
                const std::string& password,
                const std::string& graph_prefix,
                const std::string& graph_name);
    bool ResetGraph();
    bool CreateMunicipalities();
    bool CreateNodesForAllSubMunicipalities();
    bool CreateEdgesForAllSegments();
    bool CreateNodesForAllStartAndEndpoints();
    bool CreateEdgesForAllIntersections();
    std::string escape_quotes(std::string s);
};
